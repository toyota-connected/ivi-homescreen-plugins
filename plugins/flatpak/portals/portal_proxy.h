/*
 * Copyright 2023-2025 Toyota Connected North America
 * Copyright 2025 Ahmed Wafdy
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

#ifndef PORTAL_PROXY_H
#define PORTAL_PROXY_H

#include <memory>

#include "common/sdbus/sdbus.h"
#include "portal_interface.h"

/**
 * \brief This class manages the D-Bus proxies.
 * Multiple apps can own the same proxy when the last app stops,Proxy auto
 * deletes.
 */
class PortalProxy {
 public:
  explicit PortalProxy();

  ~PortalProxy() = default;

  std::shared_ptr<sdbus::IProxy> GetProxy(const PortalInterface& portal);

  void cleanup();

 private:
  std::map<PortalInterface, std::weak_ptr<sdbus::IProxy>> proxy_cache_;
  plugin_common_sdbus::SessionDBus& session_bus_;
  plugin_common_sdbus::SystemDBus& system_bus_;
  std::mutex cache_mutex_;
};

#endif  // PORTAL_PROXY_H
