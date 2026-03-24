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

#ifndef PORTAL_MANAGER_H
#define PORTAL_MANAGER_H

#include "asio/io_context.hpp"
#include "common/sdbus/sdbus.h"
#include "portal_context.h"
#include "portal_proxy.h"

namespace flatpak_plugin {
class PortalManager {
 public:
  explicit PortalManager(asio::io_context& io_context);
  ~PortalManager() = default;

  void register_application(const std::string& app_id,
                            const std::vector<PortalInterface>& interfaces);

  void unregister_application(const std::string& app_id);

  template <typename ResultCallback, typename... Args>
  void call_method(const std::string& app_id,
                   const PortalInterface& interface,
                   const std::string& method_name,
                   ResultCallback&& callback,
                   Args&&... args);

  PortalProxy& GetPortalProxy();

 private:
  asio::io_context& io_context_;
  std::unique_ptr<PortalProxy> portal_proxy_;
  std::map<std::string, PortalContext> app_context_;
  std::mutex app_mutex_;

  PortalContext& get_app_context(const std::string& app_id);
};
}  // namespace flatpak_plugin

#endif  // PORTAL_MANAGER_H
