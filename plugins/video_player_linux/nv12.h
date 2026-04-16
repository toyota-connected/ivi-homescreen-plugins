/*
 * Copyright 2020-2024 Toyota Connected North America
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <array>
#include <atomic>
#include <cstring>

#include <GLES3/gl3.h>
#include <glib.h>

#include <plugins/common/common.h>

namespace video_player_linux::nv12 {

static const GLchar* kVertexSource = R"glsl(#version 300 es
  precision highp float;

  layout(location = 0) in vec3 vertexPosition_modelspace;
  in vec2 texcoord;
  out vec2 Texcoord;
  void main()
  {
    Texcoord = texcoord;
    gl_Position.xyz = vertexPosition_modelspace;
    gl_Position.w = 1.0;
  }
)glsl";

// YUV→RGB matrix and offset live in uniforms so the same program can render
// BT.601, BT.709, and BT.2020 content; the host picks the right coefficients
// from the stream's colorimetry metadata via SetColorSpace().
static const GLchar* kFragmentSource = R"glsl(#version 300 es
  precision highp float;
  in vec2 Texcoord;
  uniform sampler2D textureY;
  uniform sampler2D textureUV;
  uniform mat3 yuv_to_rgb;
  uniform vec3 yuv_offset;
  layout(location = 0) out vec4 fragColor;
  void main() {
    vec2 coord = vec2(Texcoord.x, 1.0 - Texcoord.y);
    vec3 yuv;
    yuv.x = texture(textureY, coord).r;
    yuv.yz = texture(textureUV, coord).rg;
    yuv -= yuv_offset;
    vec3 rgb = clamp(yuv_to_rgb * yuv, 0.0, 1.0);
    fragColor = vec4(rgb, 1.0);
  }
)glsl";

// Colorimetry presets. Values match GStreamer's GstVideoColorMatrix +
// GstVideoColorRange combinations relevant to decoded NV12 content. Indexed
// into kColorPresets below.
enum class ColorSpace {
  kBt709Limited = 0,   // default — modern HD content
  kBt601Limited = 1,   // SD / legacy
  kBt2020Limited = 2,  // UHD
  kBt709Full = 3,
  kBt601Full = 4,
  kBt2020Full = 5,
};

// Packed mat3 (9 floats, column-major) + vec3 offset (3 floats) per preset.
// Matrices are derived for texel inputs in [0,1] and produce RGB in [0,1].
static constexpr GLfloat kColorPresets[6][12] = {
    // BT.709 limited range (Y:16–235, UV:16–240)
    {1.16438f, 1.16438f, 1.16438f, 0.00000f, -0.21325f, 2.11240f, 1.79274f,
     -0.53291f, 0.00000f, 0.06275f, 0.50196f, 0.50196f},
    // BT.601 limited range
    {1.16438f, 1.16438f, 1.16438f, 0.00000f, -0.39176f, 2.01723f, 1.59602f,
     -0.81297f, 0.00000f, 0.06275f, 0.50196f, 0.50196f},
    // BT.2020 non-constant-luminance limited range
    {1.16438f, 1.16438f, 1.16438f, 0.00000f, -0.18728f, 2.14177f, 1.67867f,
     -0.65042f, 0.00000f, 0.06275f, 0.50196f, 0.50196f},
    // BT.709 full range (Y:0–255, UV:0–255)
    {1.00000f, 1.00000f, 1.00000f, 0.00000f, -0.18732f, 1.85560f, 1.57480f,
     -0.46812f, 0.00000f, 0.00000f, 0.50196f, 0.50196f},
    // BT.601 full range
    {1.00000f, 1.00000f, 1.00000f, 0.00000f, -0.34414f, 1.77200f, 1.40200f,
     -0.71414f, 0.00000f, 0.00000f, 0.50196f, 0.50196f},
    // BT.2020 non-constant-luminance full range
    {1.00000f, 1.00000f, 1.00000f, 0.00000f, -0.16455f, 1.88140f, 1.47460f,
     -0.57135f, 0.00000f, 0.00000f, 0.50196f, 0.50196f},
};

class Shader {
 public:
  GLuint textureId{};
  GLuint framebuffer{};
  GLuint backTextureId{};
  GLuint backFramebuffer{};
  GLuint program{};
  GLsizei width{}, height{};
  GLuint vertex_arr_id_{};
  bool double_buffer{};

  Shader(GLsizei _width, GLsizei _height, bool _double_buffer = false)
      : width(_width), height(_height), double_buffer(_double_buffer) {
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    glGenVertexArrays(1, &vertex_arr_id_);
    glBindVertexArray(vertex_arr_id_);

    program = load_shaders();
    texY = glGetUniformLocation(program, "textureY");
    texUV = glGetUniformLocation(program, "textureUV");
    yuv_matrix_loc_ = glGetUniformLocation(program, "yuv_to_rgb");
    yuv_offset_loc_ = glGetUniformLocation(program, "yuv_offset");
    glUseProgram(program);

    glGenTextures(2, &innerTexture[0]);
    glGenTextures(1, &textureId);

    // Initialize front texture with black pixels to prevent stale content
    auto front_size =
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    auto* black_pixels = new unsigned char[front_size]();
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, black_pixels);
    delete[] black_pixels;
    glBindTexture(GL_TEXTURE_2D, 0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           textureId, 0);

    auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      spdlog::error("FramebufferStatus: 0x{:X}", status);
    }

    if (double_buffer) {
      // Back buffer for double-buffered rendering
      glGenFramebuffers(1, &backFramebuffer);
      glBindFramebuffer(GL_FRAMEBUFFER, backFramebuffer);

      glGenTextures(1, &backTextureId);
      glBindTexture(GL_TEXTURE_2D, backTextureId);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      auto back_size =
          static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
      auto* back_black = new unsigned char[back_size]();
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, back_black);
      delete[] back_black;
      glBindTexture(GL_TEXTURE_2D, 0);

      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, backTextureId, 0);

      auto backStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      if (backStatus != GL_FRAMEBUFFER_COMPLETE) {
        spdlog::error("Back FramebufferStatus: 0x{:X}", backStatus);
      }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    glGenBuffers(1, &vertex_buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    static constexpr GLfloat g_vertex_buffer_data[] = {
        -1.0f, 1.0f,  0.0f, 1.0f,  1.0f,  0.0f, 1.0f,  -1.0f, 0.0f,
        1.0f,  -1.0f, 0.0f, -1.0f, -1.0f, 0.0f, -1.0f, 1.0f,  0.0f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertex_buffer_data),
                 g_vertex_buffer_data, GL_STATIC_DRAW);

    glGenBuffers(1, &coord_buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, coord_buffer_);
    static constexpr GLfloat coord_buffer_data[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(coord_buffer_data), coord_buffer_data,
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, coord_buffer_);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);

    glFlush();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Create PBOs for async pixel upload (double-buffered ping-pong)
    glGenBuffers(2, pbo_y_);
    glGenBuffers(2, pbo_uv_);
    const auto y_size =
        static_cast<GLsizeiptr>(width) * static_cast<GLsizeiptr>(height);
    const auto uv_size = static_cast<GLsizeiptr>(width / 2) *
                         static_cast<GLsizeiptr>(height / 2) * 2;
    for (int i = 0; i < 2; ++i) {
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_y_[i]);
      glBufferData(GL_PIXEL_UNPACK_BUFFER, y_size, nullptr, GL_STREAM_DRAW);
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_uv_[i]);
      glBufferData(GL_PIXEL_UNPACK_BUFFER, uv_size, nullptr, GL_STREAM_DRAW);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  }

  ~Shader() {
    glDeleteBuffers(2, pbo_uv_);
    glDeleteBuffers(2, pbo_y_);
    glDeleteBuffers(1, &coord_buffer_);
    glDeleteBuffers(1, &vertex_buffer_);
    glDeleteVertexArrays(1, &vertex_arr_id_);
    glDeleteProgram(program);
    if (double_buffer) {
      glDeleteTextures(1, &backTextureId);
      glDeleteFramebuffers(1, &backFramebuffer);
    }
    glDeleteTextures(1, &textureId);
    glDeleteTextures(2, &innerTexture[0]);
    glDeleteFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void blit_to_front() const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, backFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glFlush();
  }

  /// @brief Return the framebuffer to render into — back when double-buffered,
  ///        front (Flutter texture) when single-buffered.
  GLuint render_target() const {
    return double_buffer ? backFramebuffer : framebuffer;
  }

  /**
   * @brief Load NV12 pixels via PBO for async DMA upload.
   * @param[in] y_buf  Pointer to Y plane data
   * @param[in] uv_buf Pointer to UV plane data
   * @param[in] y_p_s  Y pixel stride (unused)
   * @param[in] y_s    Y row stride in bytes
   * @param[in] uv_p_s UV pixel stride (unused)
   * @param[in] uv_s   UV row stride in bytes
   */
  void load_pixels(gpointer y_buf,
                   gpointer uv_buf,
                   const GLsizei y_p_s,
                   const GLsizei y_s,
                   const GLsizei uv_p_s,
                   const GLsizei uv_s) {
    (void)y_p_s;
    (void)uv_p_s;
    SPDLOG_TRACE("[VideoPlayer] load_pixels");

    const GLuint target_fb = render_target();
    glBindFramebuffer(GL_FRAMEBUFFER, target_fb);

    const auto y_plane_size =
        static_cast<GLsizeiptr>(y_s) * static_cast<GLsizeiptr>(height);
    const auto uv_plane_size =
        static_cast<GLsizeiptr>(uv_s) * static_cast<GLsizeiptr>(height / 2);

    // --- Y plane: upload via PBO ---
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_y_[pbo_index_]);
    // Orphan the old buffer so the driver can start DMA immediately
    glBufferData(GL_PIXEL_UNPACK_BUFFER, y_plane_size, nullptr, GL_STREAM_DRAW);
    void* y_mapped =
        glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, y_plane_size,
                         GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (y_mapped) {
      std::memcpy(y_mapped, y_buf, static_cast<size_t>(y_plane_size));
      glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, innerTexture[0]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, y_s);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Upload from PBO (offset 0) — GPU DMA, non-blocking
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED,
                 GL_UNSIGNED_BYTE, nullptr);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    // --- UV plane: upload via PBO ---
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_uv_[pbo_index_]);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, uv_plane_size, nullptr,
                 GL_STREAM_DRAW);
    void* uv_mapped =
        glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, uv_plane_size,
                         GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (uv_mapped) {
      std::memcpy(uv_mapped, uv_buf, static_cast<size_t>(uv_plane_size));
      glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, innerTexture[1]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, uv_s / 2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width / 2, height / 2, 0, GL_RG,
                 GL_UNSIGNED_BYTE, nullptr);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    // Ping-pong PBO index for next frame
    pbo_index_ = 1 - pbo_index_;

    auto fbo_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fbo_status != GL_FRAMEBUFFER_COMPLETE)
      spdlog::error("[VideoPlayer] Framebuffer is not complete: 0x{:X}",
                    fbo_status);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  GLuint load_shaders(const GLchar* vsource = kVertexSource,
                      const GLchar* fsource = kFragmentSource) {
    GLint result;
    GLsizei length;
    std::array<GLchar, 1000> info{};

    vertex_shader_ = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader_, 1, &vsource, nullptr);
    glCompileShader(vertex_shader_);
    glGetShaderiv(vertex_shader_, GL_COMPILE_STATUS, &result);
    if (result == GL_FALSE) {
      glGetShaderInfoLog(vertex_shader_, info.size(), &length, info.data());
      SPDLOG_ERROR("Failed to compile {}", std::string(info.data(), length));
      return 0;
    }

    fragment_shader_ = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader_, 1, &fsource, nullptr);
    glCompileShader(fragment_shader_);
    glGetShaderiv(fragment_shader_, GL_COMPILE_STATUS, &result);
    if (result == GL_FALSE) {
      glGetShaderInfoLog(fragment_shader_, info.size(), &length, info.data());
      SPDLOG_ERROR("Failed to compile {}", std::string(info.data(), length));
      return 0;
    }

    const GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertex_shader_);
    glAttachShader(shaderProgram, fragment_shader_);
    glLinkProgram(shaderProgram);

    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &result);
    if (result == GL_FALSE) {
      glGetProgramInfoLog(shaderProgram, info.size(), &length, info.data());
      SPDLOG_ERROR("Fail to link {}", std::string(info.data(), length));
      return 0;
    }

    glDetachShader(shaderProgram, vertex_shader_);
    glDetachShader(shaderProgram, fragment_shader_);
    glDeleteShader(vertex_shader_);
    glDeleteShader(fragment_shader_);
    return shaderProgram;
  }

  void draw_core() const {
    SPDLOG_TRACE("[VideoPlayer] draw_core");
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);

    // Sampler bindings and colorimetry travel with the draw — they depend on
    // which texture units load_pixels used and which stream is playing.
    glUniform1i(texY, 0);
    glUniform1i(texUV, 1);
    const auto preset = static_cast<size_t>(color_space_.load());
    glUniformMatrix3fv(yuv_matrix_loc_, 1, GL_FALSE, &kColorPresets[preset][0]);
    glUniform3fv(yuv_offset_loc_, 1, &kColorPresets[preset][9]);

    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, coord_buffer_);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    // glFlush submits commands without blocking — glFinish would stall the
    // CPU until the GPU is done, destroying any pipeline overlap.
    glFlush();
  }

  void SetColorSpace(const ColorSpace cs) { color_space_.store(cs); }

 private:
  GLint texY{};
  GLint texUV{};
  GLint yuv_matrix_loc_{-1};
  GLint yuv_offset_loc_{-1};
  std::atomic<ColorSpace> color_space_{ColorSpace::kBt709Limited};
  GLuint innerTexture[2]{};

  GLuint vertex_shader_{};
  GLuint fragment_shader_{};

  GLuint vertex_buffer_{};
  GLuint coord_buffer_{};

  // Double-buffered PBOs for async pixel upload
  GLuint pbo_y_[2]{};
  GLuint pbo_uv_[2]{};
  int pbo_index_{0};
};

}  // namespace video_player_linux::nv12
