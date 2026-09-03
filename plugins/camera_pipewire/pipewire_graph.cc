/*
 * Copyright 2020-2025 Toyota Connected North America
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

#include "pipewire_graph.h"

#include <cstring>
#include "logging/logging.h"

pipewire_graph& pipewire_graph::instance() {
  static pipewire_graph s_instance;
  return s_instance;
}

pipewire_graph::pipewire_graph() = default;

pipewire_graph::~pipewire_graph() {
  // Ensure shutdown is called in case the user forgot
  if (initialized_) {
    shutdown();
  }
}

// Callback function for detecting cameras
void pipewire_graph::on_global(void* data,
                               const uint32_t id,
                               uint32_t /*permissions*/,
                               const char* type,
                               const uint32_t version,
                               const struct spa_dict* props) {
  if (!data) {
    ihs::log::error("on_global received null data");
    return;
  }

  if (!props)
    return;

  auto* self = static_cast<pipewire_graph*>(data);

  if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
    self->handleNodeInfo(id, version, props);
  } else if (strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
    self->handlePortInfo(id, version, props);
  } else if (strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
    self->handleLinkInfo(id, version, props);
  }
}
void pipewire_graph::on_global_remove(void* data, const uint32_t id) {
  if (!data) {
    ihs::log::error("[error] on_global_remove received null data");
    return;
  }
  auto* self = static_cast<pipewire_graph*>(data);
  self->nodes_.erase(id);
  self->node_ports_.erase(id);
  self->links_.erase(id);

  // Rebuild filtered views efficiently
  self->camera_nodes_.erase(
      std::remove_if(self->camera_nodes_.begin(), self->camera_nodes_.end(),
                     [id](const NodeInfo& node) { return node.id == id; }),
      self->camera_nodes_.end());

  self->audio_nodes_.erase(
      std::remove_if(self->audio_nodes_.begin(), self->audio_nodes_.end(),
                     [id](const NodeInfo& node) { return node.id == id; }),
      self->audio_nodes_.end());

  self->all_nodes_.erase(
      std::remove_if(self->all_nodes_.begin(), self->all_nodes_.end(),
                     [id](const NodeInfo& node) { return node.id == id; }),
      self->all_nodes_.end());
}

bool pipewire_graph::initialize() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    // Already initialized
    return true;
  }

  // 1) Initialize PipeWire library (safe to call once)
  pw_init(nullptr, nullptr);

  // 2) Create the main loop, context, and core
  pw_thread_loop_ = pw_thread_loop_new("camera-loop", nullptr);
  if (!pw_thread_loop_) {
    ihs::log::error("[CameraManager] failed to create pw_main_loop.");
    return false;
  }

  // 3) Start the loop in its own thread
  if (int ret = pw_thread_loop_start(pw_thread_loop_); ret != 0) {
    ihs::log::error("[CameraManager] failed to start pw_thread_loop (err={})",
                    ret);
    pw_thread_loop_destroy(pw_thread_loop_);
    pw_thread_loop_ = nullptr;
    return false;
  }

  // 4) Lock the loop for context/core creation
  pw_thread_loop_lock(pw_thread_loop_);
  {
    // We get the underlying spa_loop from the thread loop
    if (auto* loop = pw_thread_loop_get_loop(pw_thread_loop_); !loop) {
      ihs::log::error("[CameraManager] could not get loop from threadLoop.");
    } else {
      // Create PipeWire context
      pw_context_ = pw_context_new(loop, nullptr, 0);
      if (!pw_context_) {
        ihs::log::error("[CameraManager] failed to create pw_context.");
      } else {
        // Connect to PipeWire core
        pw_core_ = pw_context_connect(pw_context_, nullptr, 0);
        if (!pw_core_) {
          ihs::log::error(
              "[CameraManager] could not connect to PipeWire core.");
        }
        pw_registry_ = pw_core_get_registry(pw_core_, PW_VERSION_REGISTRY, 0);
        static pw_registry_events registry_events = {
            .version = PW_VERSION_REGISTRY_EVENTS,
            .global = on_global,
            .global_remove = on_global_remove,
        };
        pw_registry_add_listener(pw_registry_, &registry_listener_,
                                 &registry_events, this);
      }
    }
  }
  pw_thread_loop_unlock(pw_thread_loop_);

  // Check we have context & core
  if (!pw_context_ || !pw_core_) {
    // Something failed
    pw_thread_loop_stop(pw_thread_loop_);
    pw_thread_loop_destroy(pw_thread_loop_);
    pw_thread_loop_ = nullptr;
    pw_deinit();
    return false;
  }

  initialized_ = true;
  return true;
}

void pipewire_graph::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!initialized_) {
    return;
  }

  // 1) Stop the background thread loop
  pw_thread_loop_stop(pw_thread_loop_);

  // 2) Lock while destroying
  pw_thread_loop_lock(pw_thread_loop_);
  {
    if (pw_core_) {
      pw_core_disconnect(pw_core_);
      pw_core_ = nullptr;
    }
    if (pw_context_) {
      pw_context_destroy(pw_context_);
      pw_context_ = nullptr;
    }
    spa_hook_remove(&registry_listener_);
  }
  pw_thread_loop_unlock(pw_thread_loop_);

  // 3) Destroy the thread loop
  pw_thread_loop_destroy(pw_thread_loop_);
  pw_thread_loop_ = nullptr;

  // 4) De-init PipeWire
  pw_deinit();
  initialized_ = false;
  ihs::log::info("[PipewireGraph] Shutdown completed");
}

void pipewire_graph::handleNodeInfo(const uint32_t id,
                                    const uint32_t version,
                                    const spa_dict* props) {
  std::lock_guard lock(data_mutex_);

  NodeInfo node{};
  node.id = id;
  node.version = version;
  node.name = getStringProperty(props, PW_KEY_NODE_NAME);
  node.media_class = getStringProperty(props, PW_KEY_MEDIA_CLASS);
  node.factory_name = getStringProperty(props, PW_KEY_FACTORY_NAME);
  node.is_camera = isCamera(props);
  node.is_audio = isAudio(props);

  nodes_[id] = node;
  all_nodes_.push_back(node);

  if (node.is_camera) {
    camera_nodes_.push_back(node);
  }
  if (node.is_audio) {
    audio_nodes_.push_back(node);
  }

  printDebugNodeInfo(node);
}

void pipewire_graph::handlePortInfo(const uint32_t id,
                                    uint32_t /* version */,
                                    const spa_dict* props) {
  std::lock_guard lock(data_mutex_);

  PortInfo port{};
  port.id = id;
  port.name = getStringProperty(props, PW_KEY_PORT_NAME);
  port.direction = getStringProperty(props, PW_KEY_PORT_DIRECTION);
  port.format = getStringProperty(props, PW_KEY_FORMAT_DSP);

  if (const char* node_id_str = spa_dict_lookup(props, PW_KEY_NODE_ID)) {
    port.node_id = std::stoul(node_id_str);
    node_ports_[port.node_id].push_back(port);
  }

  printDebugPortInfo(port);
}

void pipewire_graph::handleLinkInfo(const uint32_t id,
                                    uint32_t /* version */,
                                    const spa_dict* props) {
  std::lock_guard lock(data_mutex_);

  LinkInfo link{};
  link.id = id;

  const char* output_node = spa_dict_lookup(props, PW_KEY_LINK_OUTPUT_NODE);
  const char* input_node = spa_dict_lookup(props, PW_KEY_LINK_INPUT_NODE);
  const char* output_port = spa_dict_lookup(props, PW_KEY_LINK_OUTPUT_PORT);
  const char* input_port = spa_dict_lookup(props, PW_KEY_LINK_INPUT_PORT);

  if (output_node)
    link.output_node_id = std::stoul(output_node);
  if (input_node)
    link.input_node_id = std::stoul(input_node);
  if (output_port)
    link.output_port_id = std::stoul(output_port);
  if (input_port)
    link.input_port_id = std::stoul(input_port);

  links_[id] = link;
  active_links_.push_back(link);

  printDebugLinkInfo(link);
}

bool pipewire_graph::isCamera(const spa_dict* props) {
  const char* media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
  return media_class &&
         (strstr(media_class, "Video/Source") || strstr(media_class, "Camera"));
}

bool pipewire_graph::isAudio(const spa_dict* props) {
  const char* media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
  return media_class && (strstr(media_class, "Audio/Source") ||
                         strstr(media_class, "Audio/Sink"));
}

std::string pipewire_graph::getStringProperty(const spa_dict* props,
                                              const char* key) {
  const char* value = spa_dict_lookup(props, key);
  return value ? std::string(value) : std::string();
}

const std::vector<NodeInfo>& pipewire_graph::getCameraNodes() const {
  std::lock_guard lock(data_mutex_);
  return camera_nodes_;
}

const std::vector<NodeInfo>& pipewire_graph::getAudioNodes() const {
  std::lock_guard lock(data_mutex_);
  return audio_nodes_;
}

const std::vector<NodeInfo>& pipewire_graph::getAllNodes() const {
  std::lock_guard lock(data_mutex_);
  return all_nodes_;
}

const NodeInfo* pipewire_graph::getNodeById(const uint32_t id) const {
  std::lock_guard lock(data_mutex_);
  const auto it = nodes_.find(id);
  return (it != nodes_.end()) ? &it->second : nullptr;
}

const std::vector<PortInfo>& pipewire_graph::getPortsForNode(
    const uint32_t node_id) const {
  std::lock_guard lock(data_mutex_);
  const auto it = node_ports_.find(node_id);
  static const std::vector<PortInfo> empty_vector;
  return (it != node_ports_.end()) ? it->second : empty_vector;
}

const std::vector<LinkInfo>& pipewire_graph::getActiveLinks() const {
  std::lock_guard lock(data_mutex_);
  return active_links_;
}

void pipewire_graph::printDebugNodeInfo(const NodeInfo& node) {
  ihs::log::debug(
      "[PipewireGraph] Node ID: {} | Name: {} | Media Class: {} | Factory: {} "
      "| Version: {} | Camera: {} | Audio: {}",
      node.id, node.name, node.media_class, node.factory_name, node.version,
      node.is_camera, node.is_audio);
}

void pipewire_graph::printDebugPortInfo(const PortInfo& port) {
  ihs::log::debug(
      "[PipewireGraph] Port ID: {} | Node ID: {} | Name: {} | Direction: {} | "
      "Format: {}",
      port.id, port.node_id, port.name, port.direction, port.format);
}

void pipewire_graph::printDebugLinkInfo(const LinkInfo& link) {
  ihs::log::debug(
      "[PipewireGraph] Link ID: {} | Output Node: {} | Input Node: {} | Output "
      "Port: {} | Input Port: {}",
      link.id, link.output_node_id, link.input_node_id, link.output_port_id,
      link.input_port_id);
}