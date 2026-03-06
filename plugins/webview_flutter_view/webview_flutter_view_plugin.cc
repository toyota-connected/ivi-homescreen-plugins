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

#include "webview_flutter_view_plugin.h"

#include "messages.g.h"

#include <flutter/plugin_registrar.h>

#include <memory>
#include <thread>

#include "tools/encodable.h"
#include "tools/logging.h"

#include "wrapper/cef_library_loader.h"

namespace plugin_webview_flutter {

std::vector<std::unique_ptr<WebviewPlatformView>>
    WebviewFlutterPlugin::m_WebViews;

GLuint LoadShader(const GLchar* shaderSrc, const GLenum type) {
  // Create the shader object
  const GLuint shader = glCreateShader(type);
  if (shader == 0)
    return 0;
  glShaderSource(shader, 1, &shaderSrc, nullptr);
  glCompileShader(shader);
  GLint compiled;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    GLint infoLen = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
    if (infoLen > 1) {
      auto* infoLog = static_cast<GLchar*>(
          malloc(sizeof(char) * static_cast<unsigned long>(infoLen)));
      glGetShaderInfoLog(shader, infoLen, nullptr, infoLog);
      spdlog::error("Error compiling shader:\n{}\n", infoLog);
      free(infoLog);
    }
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

// static
void WebviewFlutterPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrar* registrar) {
  auto plugin = std::make_unique<WebviewFlutterPlugin>();

  plugin->m_InstanceManagerHostApi.SetUp(registrar->messenger(),
                                         &plugin->m_InstanceManagerHostApi);
  plugin->m_WebStorageHostApi.SetUp(registrar->messenger(),
                                    &plugin->m_WebStorageHostApi);
  plugin->m_WebViewHostApi.SetUp(registrar->messenger(),
                                 &plugin->m_WebViewHostApi);
  plugin->m_WebSettingsHostApi.SetUp(registrar->messenger(),
                                     &plugin->m_WebSettingsHostApi);
  plugin->m_WebChromeClientHostApi.SetUp(registrar->messenger(),
                                         &plugin->m_WebChromeClientHostApi);
  plugin->m_WebViewClientHostApi.SetUp(registrar->messenger(),
                                       &plugin->m_WebViewClientHostApi);
  plugin->m_DownloadListenerHostApi.SetUp(registrar->messenger(),
                                          &plugin->m_DownloadListenerHostApi);
  plugin->m_JavaScriptChannelHostApi.SetUp(registrar->messenger(),
                                           &plugin->m_JavaScriptChannelHostApi);
  plugin->m_CookieManagerHostApi.SetUp(registrar->messenger(),
                                       &plugin->m_CookieManagerHostApi);

  registrar->AddPlugin(std::move(plugin));
}

void WebviewPlatformView::GetViewRect(CefRefPtr<CefBrowser> /* browser */,
                                      CefRect& rect) {
  spdlog::debug("[webview_flutter] GetViewRect");
  rect.width = static_cast<int>(width_);
  rect.height = static_cast<int>(height_);
}

void WebviewPlatformView::OnPaint(CefRefPtr<CefBrowser> /* browser */,
                                  PaintElementType type,
                                  const RectList& /* dirtyRects */,
                                  const void* buffer,
                                  int width,
                                  int height) {
  spdlog::trace("[webview_flutter] OnPaint, width: {}, height: {}, type: {}",
                width, height, (uint8_t)type);
  if (eglGetCurrentContext() != egl_context_) {
    eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_);
  }

  glBindTexture(GL_TEXTURE_2D, gl_texture_);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, buffer);
  glBindTexture(GL_TEXTURE_2D, 0);

  glUseProgram(programObject_);
  glUniform1i(glGetUniformLocation(programObject_, "texture1"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gl_texture_);

  glBindVertexArray(VAO);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

  eglSwapBuffers(egl_display_, egl_surface_);
  eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

  wl_subsurface_place_below(subsurface_, parent_surface_);
  wl_subsurface_set_position(subsurface_, 0, 0);
  wl_surface_commit(surface_);
}

void WebviewPlatformView::OnAcceleratedPaint(
    CefRefPtr<CefBrowser> /* browser */,
    PaintElementType /* type */,
    const RectList& /* dirtyRects */,
    const CefAcceleratedPaintInfo& /* info */) {
  spdlog::trace("[webview_flutter] OnAcceleratedPaint");
}

WebviewFlutterPlugin::WebviewFlutterPlugin() {}

void WebviewFlutterPlugin::PlatformViewCreate(
    int32_t id,
    std::string viewType,
    int32_t direction,
    double top,
    double left,
    double width,
    double height,
    const std::vector<uint8_t>& params,
    std::string assetDirectory,
    FlutterDesktopEngineRef engine,
    PlatformViewAddListener addListener,
    PlatformViewRemoveListener removeListener,
    void* platform_view_context) {
  auto webview = std::make_unique<WebviewPlatformView>(
      id, std::move(viewType), direction, top, left, width, height, params,
      std::move(assetDirectory), engine, addListener, removeListener,
      platform_view_context);

  m_WebViews.emplace_back(std::move(webview));
}

WebviewPlatformView::WebviewPlatformView(
    const int32_t id,
    std::string viewType,
    const int32_t direction,
    const double top,
    const double left,
    const double width,
    const double height,
    const std::vector<uint8_t>& /* params */,
    std::string assetDirectory,
    FlutterDesktopEngineState* state,
    const PlatformViewAddListener addListener,
    const PlatformViewRemoveListener removeListener,
    void* platform_view_context)
    : PlatformView(id,
                   std::move(viewType),
                   direction,
                   top,
                   left,
                   width,
                   height),
      id_(id),
      platformViewsContext_(platform_view_context),
      removeListener_(removeListener),
      flutterAssetsPath_(std::move(assetDirectory)),
      callback_(nullptr),
      width_(width),
      height_(height) {
  spdlog::debug(
      "++WebviewPlatformView::WebviewPlatformView: Top: {}, Left: {}, Width: "
      "{}, Height: {}, direction: {}, viewType: {}",
      top, left, width, height, direction, viewType);

  const auto flutter_view = state->view_controller->view;

  /* Setup Wayland subsurface */
  display_ = flutter_view->GetDisplay()->GetDisplay();
  parent_surface_ = flutter_view->GetWindow()->GetBaseSurface();
  surface_ =
      wl_compositor_create_surface(flutter_view->GetDisplay()->GetCompositor());

  parent_surface_ = flutter_view->GetWindow()->GetBaseSurface();
  subsurface_ = wl_subcompositor_get_subsurface(
      flutter_view->GetDisplay()->GetSubCompositor(), surface_,
      parent_surface_);

  // wl_subsurface_set_sync(subsurface_);
  wl_subsurface_set_desync(subsurface_);
  wl_subsurface_set_position(subsurface_, static_cast<int32_t>(top),
                             static_cast<int32_t>(left));
  // wl_subsurface_place_above(subsurface_, parent_surface_);
  wl_subsurface_place_below(subsurface_, surface_);
  wl_surface_commit(parent_surface_);

  addListener(platformViewsContext_, id, &platform_view_listener_, this);

  cef_thread_ = std::thread(&WebviewPlatformView::CefThreadMain, this);

  // on_frame(this, callback_, 0);
}

WebviewPlatformView::~WebviewPlatformView() {
  spdlog::debug("[webview_flutter] ~WebviewPlatformView");
  removeListener_(platformViewsContext_, id_);
}

void WebviewPlatformView::CefThreadMain() {
  std::vector<const char*> args;
  args.reserve(11);
  args.push_back("homescreen");
  args.push_back("--use-views");
  args.push_back("--use-ozone");
  args.push_back("--enable-features=UseOzonePlatform");
  args.push_back("--ozone-platform=wayland");
  args.push_back("--log-level=0");
  args.push_back("--v=1");
  args.push_back("--use-gl=egl");
  args.push_back("--in-process-gpu");
  // args.push_back("--off-screen-rendering-enabled"); // For OnAcceleratedPaint
  // callback args.push_back("--shared-texture-enabled"); // For
  // OnAcceleratedPaint callback

  // Setup EGL objects
  egl_display_ = eglGetDisplay(display_);
  assert(egl_display_);
  egl_window_ = wl_egl_window_create(surface_, 800, 600);
  assert(egl_window_);
  InitializeEGL();
  egl_surface_ =
      eglCreateWindowSurface(egl_display_, egl_config_, egl_window_, nullptr);

  eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_);
  InitializeScene();

  // Load libcef.so
  std::string libcef_path_str = "libcef.so";
  std::filesystem::path libcef_file_path(libcef_path_str);
  spdlog::debug("[webview_flutter] cef_load_library");
  int cef_load_ok = cef_load_library(libcef_path_str.c_str());
  if (!cef_load_ok) {
    exit(-1);
  }
  spdlog::debug("[webview_flutter] cef_load_library OK!");

  //  Set-up main args and settings for CEF
  CefMainArgs main_args(static_cast<int>(args.size()),
                        const_cast<char**>(args.data()));

  // Specify CEF global settings here.
  CefSettings settings;

  settings.no_sandbox = false;
  settings.windowless_rendering_enabled = true;
  settings.log_severity = LOGSEVERITY_INFO;

  std::string root_cache_path_str =
      std::string(CEF_ROOT) + "/.config/cef_user_data";
  const char* root_cache_path = root_cache_path_str.c_str();
  CefString(&settings.root_cache_path).FromASCII(root_cache_path);

  std::string resource_path_str = std::string(CEF_ROOT) + "/Resources";
  const char* resource_path = resource_path_str.c_str();
  CefString(&settings.resources_dir_path).FromASCII(resource_path);

  const char* browser_subprocess_path =
      "/usr/local/bin/webview_flutter_subprocess";
  CefString(&settings.browser_subprocess_path)
      .FromASCII(browser_subprocess_path);

  spdlog::debug("[webview_flutter] ++CefInitialize");
  if (!CefInitialize(main_args, settings, this, nullptr)) {
    int error_code;
    error_code = CefGetExitCode();
    spdlog::error("[webview_flutter] CefInitialize: {}", error_code);
    exit(EXIT_FAILURE);
  }
  spdlog::debug("[webview_flutter] --CefInitialize");

  // Run the CEF message loop. This will block until CefQuitMessageLoop() is
  // called.
  spdlog::debug("[webview_cef_thread] ++CefRunMessageLoop");
  CefRunMessageLoop();
  spdlog::debug("[webview_cef_thread] --CefRunMessageLoop");

  // Shut down CEF.
  spdlog::debug("[webview_cef_thread] ++CefShutdown");
  CefShutdown();
  spdlog::debug("[webview_cef_thread] --CefShutdown");
}

WebviewFlutterPlugin::~WebviewFlutterPlugin() {
  spdlog::debug(
      "[webview_cef_thread] WebviewFlutterPlugin::~WebviewFlutterPlugin");
  CefPostTask(TID_UI, base::BindOnce(CefQuitMessageLoop));
};

void WebviewPlatformView::OnContextInitialized() {
  spdlog::debug("[webview_flutter] WebviewPlatformView::OnContextInitialized");
  CefWindowInfo window_info;
  window_info.SetAsWindowless(true);
  // window_info.shared_texture_enabled = true; // For OnAcceleratedPaint
  // callback

  CefBrowserSettings browserSettings;
  browserSettings.windowless_frame_rate = 60;  // 30 is default

  spdlog::debug("[webview_flutter] CreateBrowserSync++");
  browser_ = CefBrowserHost::CreateBrowserSync(
      window_info, this, "https://www.google.com", browserSettings, nullptr,
      nullptr);
  spdlog::debug("[webview_flutter] CreateBrowserSync--");
}

void WebviewPlatformView::InitializeScene() {
  constexpr GLchar vShaderStr[] =
      "#version 320 es\n"
      "layout (location = 0) in vec3 aPos;\n"
      "layout (location = 1) in vec3 aColor;\n"
      "layout (location = 2) in vec2 aTexCoord;\n"
      "out vec3 ourColor;\n"
      "out vec2 TexCoord;\n"
      "void main()\n"
      "{\n"
      "gl_Position = vec4(aPos, 1.0);\n"
      "ourColor = aColor;\n"
      "TexCoord = aTexCoord;\n"
      "}\n";
  constexpr GLchar fShaderStr[] =
      "#version 320 es\n"
      "precision mediump float;\n"
      "out vec4 FragColor;\n"
      "in vec3 ourColor;\n"
      "in vec2 TexCoord;\n"
      "uniform sampler2D ourTexture;\n"
      "void main()\n"
      "{\n"
      "    FragColor = texture(ourTexture, TexCoord).bgra;\n"
      "}\n";

  glClearColor(0.0f, 0.0f, 0.4f, 0.0f);
  glGenFramebuffers(1, &framebuffer_);
  glGenTextures(1, &gl_texture_);
  glGenRenderbuffers(1, &depthrenderbuffer_);

  const GLuint vertexShader = LoadShader(vShaderStr, GL_VERTEX_SHADER);
  const GLuint fragmentShader = LoadShader(fShaderStr, GL_FRAGMENT_SHADER);

  const GLuint programObject = glCreateProgram();
  if (programObject == 0)
    return;

  glAttachShader(programObject, vertexShader);
  glAttachShader(programObject, fragmentShader);

  glLinkProgram(programObject);

  GLint linked;
  glGetProgramiv(programObject, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLint infoLen = 0;
    glGetProgramiv(programObject, GL_INFO_LOG_LENGTH, &infoLen);
    if (infoLen > 1) {
      auto* infoLog = static_cast<GLchar*>(
          malloc(sizeof(char) * static_cast<unsigned long>(infoLen)));
      glGetProgramInfoLog(programObject, infoLen, nullptr, infoLog);
      spdlog::error("Error linking program:\n{}\n", infoLog);
      free(infoLog);
    }
    glDeleteProgram(programObject);
    return;
  }

  programObject_ = programObject;

  float vertices[] = {
      // positions          // colors           // texture coords
      1.0f,  1.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,  // top right
      1.0f,  -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,  // bottom right
      -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,  // bottom left
      -1.0f, 1.0f,  0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f   // top left
  };
  unsigned int indices[] = {
      0, 1, 3,  // first triangle
      1, 2, 3   // second triangle
  };

  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);
  glGenBuffers(1, &EBO);

  glBindVertexArray(VAO);

  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);

  // position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
  // color attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  // texture coord attribute
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void*)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);
}

void WebviewPlatformView::InitializeEGL() {
  EGLint major, minor;
  EGLBoolean ret = eglInitialize(egl_display_, &major, &minor);
  assert(ret == EGL_TRUE);

  ret = eglBindAPI(EGL_OPENGL_ES_API);
  assert(ret == EGL_TRUE);

  EGLint count;
  eglGetConfigs(egl_display_, nullptr, 0, &count);
  assert(count);
  spdlog::debug("[webview_flutter] InitializeEGL: EGL has {} configs", count);

  auto* configs = static_cast<EGLConfig*>(
      calloc(static_cast<size_t>(count), sizeof(EGLConfig)));
  assert(configs);

  EGLint n;
  ret = eglChooseConfig(egl_display_, kEglConfigAttribs.data(), configs, count,
                        &n);
  assert(ret && n >= 1);

  EGLint size;
  for (EGLint i = 0; i < n; i++) {
    eglGetConfigAttrib(egl_display_, configs[i], EGL_BUFFER_SIZE, &size);
    spdlog::debug(
        "[webview_flutter] InitializeEGL: Buffer size for config {} is {}", i,
        size);
    if (buffer_size_ <= size) {
      memcpy(&egl_config_, &configs[i], sizeof(EGLConfig));
      break;
    }
  }
  free(configs);
  if (egl_config_ == nullptr) {
    spdlog::critical(
        "[webview_flutter] InitializeEGL: did not find config with buffer size "
        "{}",
        buffer_size_);
    assert(false);
  }

  egl_context_ = eglCreateContext(egl_display_, egl_config_, EGL_NO_CONTEXT,
                                  kEglContextAttribs.data());
  assert(egl_context_);
  spdlog::debug("[webview_flutter] InitializeEGL: Context={}", egl_context_);
}

//
//
// WebviewFlutterInstanceManagerHostApi
//
//

WebviewFlutterInstanceManagerHostApi::~WebviewFlutterInstanceManagerHostApi() =
    default;

std::optional<FlutterError> WebviewFlutterInstanceManagerHostApi::Clear() {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterInstanceManagerHostApi: Clear");
  return std::nullopt;
}

//
//
//  WebviewFlutterWebStorageHostApi
//
//

WebviewFlutterWebStorageHostApi::~WebviewFlutterWebStorageHostApi() = default;

std::optional<FlutterError> WebviewFlutterWebStorageHostApi::Create(
    int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebStorageHostApi: Create, instance_id: "
      "{}",
      instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebStorageHostApi::DeleteAllData(
    int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebStorageHostApi: DeleteAllData, "
      "instance_id: {}",
      instance_id);
  return std::nullopt;
}

//
//
// WebviewFlutterWebViewHostApi
//
//

WebviewFlutterWebViewHostApi::~WebviewFlutterWebViewHostApi() = default;

std::optional<FlutterError> WebviewFlutterWebViewHostApi::Create(
    int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: Create, instance_id: {}",
      instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::LoadData(
    int64_t instance_id,
    const std::string& /* data */,
    const std::string* mime_type,
    const std::string* encoding) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: LoadData, instance_id: "
      "{}, mime_type: {}, encoding: "
      "{}",
      instance_id, *mime_type, *encoding);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::LoadDataWithBaseUrl(
    int64_t instance_id,
    const std::string* base_url,
    const std::string& /* data */,
    const std::string* mime_type,
    const std::string* encoding,
    const std::string* history_url) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: LoadDataWithBaseUrl, "
      "instance_id: {}, base_url: {}, "
      "mime_type: "
      "{}, encoding: {}, history_url: {}",
      instance_id, *base_url, *mime_type, *encoding, *history_url);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::LoadUrl(
    int64_t instance_id,
    const std::string& url,
    const flutter::EncodableMap& headers) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: LoadUrl, instance_id: "
      "{}, url: {}",
      instance_id, url);
  if (!headers.empty()) {
    plugin_common::Encodable::PrintFlutterEncodableMap("headers", headers);
  }
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::PostUrl(
    int64_t instance_id,
    const std::string& url,
    const std::vector<uint8_t>& /* data */) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: PostUrl: instance_id: "
      "{}, url: {}",
      instance_id, url);
  return std::nullopt;
}

ErrorOr<std::optional<std::string>> WebviewFlutterWebViewHostApi::GetUrl(
    int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: GetUrl, instance_id: {}",
      instance_id);
  // TODO - set favorite in test case calls this
  return {"https://www.google.com"};
}

ErrorOr<bool> WebviewFlutterWebViewHostApi::CanGoBack(int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: CanGoBack, instance_id: "
      "{}",
      instance_id);
  return {true};
}

ErrorOr<bool> WebviewFlutterWebViewHostApi::CanGoForward(int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: CanGoForward, "
      "instance_id: {}",
      instance_id);
  return {true};
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::GoBack(
    int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: GoBack, instance_id: {}",
      instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::GoForward(
    int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: GoForward, instance_id: "
      "{}",
      instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::Reload(
    int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: Reload, instance_id: {}",
      instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::ClearCache(
    int64_t instance_id,
    bool include_disk_files) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: ClearCache, "
      "instance_id: {}, include_disk_files: {}",
      instance_id, include_disk_files);
  return std::nullopt;
}

void WebviewFlutterWebViewHostApi::EvaluateJavascript(
    int64_t instance_id,
    const std::string& javascript_string,
    std::function<
        void(ErrorOr<std::optional<std::string>> reply)> /* result */) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: EvaluateJavascript, "
      "instance_id: {}, "
      "javascript_string: {}",
      instance_id, javascript_string);
}

ErrorOr<std::optional<std::string>> WebviewFlutterWebViewHostApi::GetTitle(
    int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: GetTitle, instance_id: "
      "{}",
      instance_id);
  return {std::nullopt};
}
std::optional<FlutterError> WebviewFlutterWebViewHostApi::ScrollTo(
    int64_t instance_id,
    int64_t x,
    int64_t y) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: ScrollTo, instance_id: "
      "{}, x: {}, y: {}",
      instance_id, x, y);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::ScrollBy(
    int64_t instance_id,
    int64_t x,
    int64_t y) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: ScrollBy, instance_id: "
      "{}, x: {}, y: {}",
      instance_id, x, y);
  return std::nullopt;
}

ErrorOr<int64_t> WebviewFlutterWebViewHostApi::GetScrollX(int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: GetScrollX, "
      "instance_id: {}",
      instance_id);
  return {0};
}

ErrorOr<int64_t> WebviewFlutterWebViewHostApi::GetScrollY(int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: GetScrollY, "
      "instance_id: {}",
      instance_id);
  return {0};
}

ErrorOr<WebViewPoint> WebviewFlutterWebViewHostApi::GetScrollPosition(
    int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: GetScrollPosition, "
      "instance_id: {}",
      instance_id);
  return {WebViewPoint{0, 0}};
}

std::optional<FlutterError>
WebviewFlutterWebViewHostApi::SetWebContentsDebuggingEnabled(bool enabled) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: "
      "SetWebContentsDebuggingEnabled, enabled: {}",
      enabled);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::SetWebViewClient(
    int64_t instance_id,
    int64_t web_view_client_instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: SetWebViewClient, "
      "instance_id: {}, "
      "web_view_client_instance_id: {}",
      instance_id, web_view_client_instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::AddJavaScriptChannel(
    int64_t instance_id,
    int64_t java_script_channel_instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: AddJavaScriptChannel, "
      "instance_id: {}, "
      "java_script_channel_instance_id: {}",
      instance_id, java_script_channel_instance_id);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebViewHostApi::RemoveJavaScriptChannel(
    int64_t instance_id,
    int64_t java_script_channel_instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: "
      "RemoveJavaScriptChannel, instance_id: {}, "
      "java_script_channel_instance_id: {}",
      instance_id, java_script_channel_instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::SetDownloadListener(
    int64_t instance_id,
    const int64_t* listener_instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: SetDownloadListener, "
      "instance_id: {}, "
      "listener_instance_id: {}",
      instance_id, fmt::ptr(listener_instance_id));
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::SetWebChromeClient(
    int64_t instance_id,
    const int64_t* client_instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: SetWebChromeClient, "
      "instance_id: {}, "
      "client_instance_id: {}",
      instance_id, fmt::ptr(client_instance_id));
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewHostApi::SetBackgroundColor(
    int64_t instance_id,
    int64_t color) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewHostApi: SetWebChromeClient, "
      "instance_id: {}, color: 0x{:08}",
      instance_id, color);
  return std::nullopt;
}

//
//
// WebviewFlutterWebSettingsHostApi
//
//

WebviewFlutterWebSettingsHostApi::~WebviewFlutterWebSettingsHostApi() = default;

std::optional<FlutterError> WebviewFlutterWebSettingsHostApi::Create(
    int64_t instance_id,
    int64_t web_view_instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: Create, "
      "instance_id: {}, web_view_instance_id: {}",
      instance_id, web_view_instance_id);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebSettingsHostApi::SetDomStorageEnabled(int64_t instance_id,
                                                       bool flag) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: "
      "SetDomStorageEnabled, instance_id: {}, flag: {}",
      instance_id, flag);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebSettingsHostApi::SetJavaScriptCanOpenWindowsAutomatically(
    int64_t instance_id,
    bool flag) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: "
      "SetJavaScriptCanOpenWindowsAutomatically, "
      "instance_id: {}, flag: {}",
      instance_id, flag);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebSettingsHostApi::SetSupportMultipleWindows(int64_t instance_id,
                                                            bool support) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: "
      "SetSupportMultipleWindows, instance_id: {}. support: "
      "{}",
      instance_id, support);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebSettingsHostApi::SetJavaScriptEnabled(int64_t instance_id,
                                                       bool flag) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: "
      "SetJavaScriptEnabled, instance_id: {}, flag: {}",
      instance_id, flag);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebSettingsHostApi::SetUserAgentString(
    int64_t instance_id,
    const std::string* user_agent_string) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: SetUserAgentString, "
      "instance_id: {}, "
      "user_agent_string: {}",
      instance_id, *user_agent_string);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebSettingsHostApi::SetMediaPlaybackRequiresUserGesture(
    int64_t instance_id,
    bool require) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: "
      "SetMediaPlaybackRequiresUserGesture, instance_id: {}, "
      "require: {}",
      instance_id, require);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebSettingsHostApi::SetSupportZoom(
    int64_t instance_id,
    bool support) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: SetSupportZoom, "
      "instance_id: {}, support: {}",
      instance_id, support);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebSettingsHostApi::SetLoadWithOverviewMode(int64_t instance_id,
                                                          bool overview) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: "
      "SetLoadWithOverviewMode, instance_id: {}, overview: "
      "{}",
      instance_id, overview);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebSettingsHostApi::SetUseWideViewPort(int64_t instance_id,
                                                     bool use) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: SetUseWideViewPort, "
      "instance_id: {}, use: {}",
      instance_id, use);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebSettingsHostApi::SetDisplayZoomControls(int64_t instance_id,
                                                         bool enabled) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: "
      "SetDisplayZoomControls, instance_id: {}, enabled: {}",
      instance_id, enabled);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebSettingsHostApi::SetBuiltInZoomControls(int64_t instance_id,
                                                         bool enabled) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: "
      "SetBuiltInZoomControls, instance_id: {}, enabled: {}",
      instance_id, enabled);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebSettingsHostApi::SetAllowFileAccess(int64_t instance_id,
                                                     bool enabled) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: SetAllowFileAccess, "
      "instance_id: {}, enabled: {}",
      instance_id, enabled);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebSettingsHostApi::SetTextZoom(
    int64_t instance_id,
    int64_t text_zoom) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: SetTextZoom, "
      "instance_id: {}, text_zoom: {}",
      instance_id, text_zoom);
  return std::nullopt;
}

ErrorOr<std::string> WebviewFlutterWebSettingsHostApi::GetUserAgentString(
    int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebSettingsHostApi: GetUserAgentString, "
      "instance_id: {}",
      instance_id);
  return {""};
}

//
//
// WebviewFlutterWebChromeClientHostApi
//
//

WebviewFlutterWebChromeClientHostApi::~WebviewFlutterWebChromeClientHostApi() =
    default;

std::optional<FlutterError> WebviewFlutterWebChromeClientHostApi::Create(
    int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebChromeClientHostApi: Create, "
      "instance_id: {}",
      instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebChromeClientHostApi::
    SetSynchronousReturnValueForOnShowFileChooser(int64_t instance_id,
                                                  bool value) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebChromeClientHostApi: "
      "SetSynchronousReturnValueForOnShowFileChooser, "
      "instance_id: {}, value: {}",
      instance_id, value);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebChromeClientHostApi::
    SetSynchronousReturnValueForOnConsoleMessage(int64_t instance_id,
                                                 bool value) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebChromeClientHostApi: "
      "SetSynchronousReturnValueForOnConsoleMessage, "
      "instance_id: {}, value: {}",
      instance_id, value);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebChromeClientHostApi::SetSynchronousReturnValueForOnJsAlert(
    int64_t instance_id,
    bool value) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebChromeClientHostApi: "
      "SetSynchronousReturnValueForOnJsAlert, instance_id: "
      "{}, value: {}",
      instance_id, value);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebChromeClientHostApi::SetSynchronousReturnValueForOnJsConfirm(
    int64_t instance_id,
    bool value) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebChromeClientHostApi: "
      "SetSynchronousReturnValueForOnJsConfirm, instance_id: "
      "{}, value: {}",
      instance_id, value);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterWebChromeClientHostApi::SetSynchronousReturnValueForOnJsPrompt(
    int64_t instance_id,
    bool value) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebChromeClientHostApi: "
      "SetSynchronousReturnValueForOnJsPrompt: instance_id: "
      "{}, value: {}",
      instance_id, value);
  return std::nullopt;
}

//
//
// WebviewFlutterCookieManagerHostApi
//
//

WebviewFlutterCookieManagerHostApi::~WebviewFlutterCookieManagerHostApi() =
    default;

std::optional<FlutterError> WebviewFlutterCookieManagerHostApi::AttachInstance(
    int64_t instance_identifier) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterCookieManagerHostApiAttachInstance, "
      "instance_identifier: {}",
      instance_identifier);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterCookieManagerHostApi::SetCookie(
    int64_t identifier,
    const std::string& url,
    const std::string& value) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterCookieManagerHostApiSetCookie, "
      "identifier: {}, url: {}, value: {}",
      identifier, url, value);
  return std::nullopt;
}

void WebviewFlutterCookieManagerHostApi::RemoveAllCookies(
    int64_t identifier,
    const std::function<void(ErrorOr<bool> reply)> result) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterCookieManagerHostApiRemoveAllCookies, "
      "identifier: {}",
      identifier);
  result(true);
}

std::optional<FlutterError>
WebviewFlutterCookieManagerHostApi::SetAcceptThirdPartyCookies(
    int64_t identifier,
    int64_t web_view_identifier,
    bool accept) {
  spdlog::debug(
      "[webview_flutter] "
      "WebviewFlutterCookieManagerHostApiSetAcceptThirdPartyCookies, "
      "identifier: {}, "
      "web_view_identifier: {}, accept: {}",
      identifier, web_view_identifier, accept);
  return std::nullopt;
}

//
//
// WebviewFlutterWebViewClientHostApi
//
//

WebviewFlutterWebViewClientHostApi::~WebviewFlutterWebViewClientHostApi() =
    default;

std::optional<FlutterError> WebviewFlutterWebViewClientHostApi::Create(
    int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterDownloadListenerHostApi: Create, "
      "instance_id: {}",
      instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterWebViewClientHostApi::
    SetSynchronousReturnValueForShouldOverrideUrlLoading(int64_t instance_id,
                                                         bool value) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterWebViewClientHostApi: "
      "SetSynchronousReturnValueForShouldOverrideUrlLoading, "
      "instance_id: {}, value: {}",
      instance_id, value);
  return std::nullopt;
}

//
//
// WebviewFlutterDownloadListenerHostApi
//
//

WebviewFlutterDownloadListenerHostApi::
    ~WebviewFlutterDownloadListenerHostApi() = default;

std::optional<FlutterError> WebviewFlutterDownloadListenerHostApi::Create(
    int64_t instance_id) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterDownloadListenerHostApi: Create, "
      "instance_id: {}",
      instance_id);
  return std::nullopt;
}

//
//
// WebviewFlutterJavaScriptChannelHostApi
//
//

WebviewFlutterJavaScriptChannelHostApi::
    ~WebviewFlutterJavaScriptChannelHostApi() = default;

std::optional<FlutterError> WebviewFlutterJavaScriptChannelHostApi::Create(
    int64_t instance_id,
    const std::string& channel_name) {
  spdlog::debug(
      "[webview_flutter] WebviewFlutterJavaScriptChannelHostApi: Create, "
      "instance_id: {}, channel_name: {}",
      instance_id, channel_name);
  return std::nullopt;
}

//
//
//
//
//
void WebviewPlatformView::on_resize(double /* width */,
                                    double /* height */,
                                    void* /* data */) {
  spdlog::debug("[webview_flutter] on_resize");
  // if (const auto plugin = static_cast<WebviewPlatformView*>(data)) {
  //   plugin->width_ = static_cast<int32_t>(width);
  //   plugin->height_ = static_cast<int32_t>(height);
  //   spdlog::debug("[webview_flutter] Resize: {} {}", width, height);
  // }
}

void WebviewPlatformView::on_set_direction(const int32_t direction,
                                           void* data) {
  spdlog::debug("[webview_flutter] on_set_direction");
  if (auto plugin = static_cast<WebviewPlatformView*>(data)) {
    plugin->direction_ = direction;
    spdlog::debug("[webview_flutter] SetDirection: {}", plugin->direction_);
  }
}

void WebviewPlatformView::on_set_offset(const double left,
                                        const double top,
                                        void* data) {
  spdlog::debug("[webview_flutter] on_set_offset");
  if (const auto plugin = static_cast<WebviewPlatformView*>(data)) {
    plugin->left_ = static_cast<int32_t>(left);
    plugin->top_ = static_cast<int32_t>(top);
    if (plugin->subsurface_) {
      spdlog::debug("[webview_flutter] SetOffset: left: {}, top: {}",
                    plugin->left_, plugin->top_);
      wl_subsurface_set_position(plugin->subsurface_, plugin->left_,
                                 plugin->top_);
      if (!plugin->callback_) {
        on_frame(plugin, plugin->callback_, 0);
      }
    }
  }
}

void WebviewPlatformView::on_touch(int32_t action,
                                   int32_t point_count,
                                   const size_t point_data_size,
                                   const double* point_data,
                                   void* data) {
  spdlog::trace(
      "[webview_flutter] on_touch: action: {}  point_count: {}  data_size: {}  "
      "location: {}x:{}y",
      action, point_count, point_data_size, *(point_data + 7),
      *(point_data + 8));
  if (const auto plugin = static_cast<WebviewPlatformView*>(data); plugin) {
    cef_touch_event_type_t type;
    float data_x, data_y;
    int id = 0;

    switch (action) {
      case 0:
        type = CEF_TET_PRESSED;
        break;
      case 1:
        type = CEF_TET_RELEASED;
        break;
      case 2:
        type = CEF_TET_MOVED;
        break;
      default:
        type = CEF_TET_CANCELLED;
        break;
    }

    data_x = *(point_data + 7);
    data_y = *(point_data + 8);

    CefPostTask(TID_UI,
                base::BindOnce(&WebviewPlatformView::SendTouch, id, data_x,
                               data_y, type, plugin->browser_->GetHost()));
  }
}

void WebviewPlatformView::SendTouch(int id,
                                    float data_x,
                                    float data_y,
                                    cef_touch_event_type_t action_type,
                                    CefRefPtr<CefBrowserHost> host) {
  CefTouchEvent touch_event;
  touch_event.id = id;
  touch_event.x = data_x;
  touch_event.y = data_y;
  touch_event.type = action_type;

  spdlog::trace("[webview_flutter] SendTouch");
  host->SendTouchEvent(touch_event);
}

void WebviewPlatformView::on_dispose(bool /* hybrid */, void* data) {
  spdlog::debug("[webview_flutter] on_dispose");
  const auto plugin = static_cast<WebviewPlatformView*>(data);
  if (plugin->callback_) {
    wl_callback_destroy(plugin->callback_);
    plugin->callback_ = nullptr;
  }

  if (plugin->subsurface_) {
    wl_subsurface_destroy(plugin->subsurface_);
    plugin->subsurface_ = nullptr;
  }

  if (plugin->surface_) {
    wl_surface_destroy(plugin->surface_);
    plugin->surface_ = nullptr;
  }
}

const struct platform_view_listener
    WebviewPlatformView::platform_view_listener_ = {
        .resize = on_resize,
        .set_direction = on_set_direction,
        .set_offset = on_set_offset,
        .on_touch = on_touch,
        .dispose = on_dispose,
        .accept_gesture = nullptr,
        .reject_gesture = nullptr,
};

void WebviewPlatformView::on_frame(void* /* data */,
                                   wl_callback* /* callback */,
                                   const uint32_t /* time */) {
  spdlog::debug("[webview_flutter] on_frame");
}

const wl_callback_listener WebviewPlatformView::frame_listener = {.done =
                                                                      on_frame};

void WebviewPlatformView::DrawFrame(uint32_t /* time */) const {}

}  // namespace plugin_webview_flutter
