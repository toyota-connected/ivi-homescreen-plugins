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

#ifndef IVI_HOMESCREEN_ACCESS_PORTAL_H
#define IVI_HOMESCREEN_ACCESS_PORTAL_H

#include <string>

#include <flutter/event_channel.h>
#include "../../messages.g.h"
#include "../portal_manager.h"
#include "common/sdbus/sdbus.h"

namespace flatpak_plugin {
enum PermissionStatus {
  GRANTED,  // "yes"
  DENIED,   // "no"
  ASK,      // "ask"
  NOT_SET   // No entry exists
};

class PermissionsPortal {
 public:
  explicit PermissionsPortal(asio::io_context& io_context);

  ~PermissionsPortal() = default;

  /**
   * \brief Check all permissions for app to launch.
   * \param app Application that needs access to a resource.
   * \param permissions Permission assigned to app could be 'yes','no' or 'ask'.
   * \param callback Callback function for async operations.
   */
  void CheckAllPermissions(
      const std::string& app,
      const std::vector<std::string>& permissions,
      const std::function<void(std::map<std::string, PermissionStatus>)>&
          callback) const;

  /**
   * \brief Sets permission for app.
   * \param table Table of resources for apps which could be either a device or
   * a feature. \param id Identifier for the resource in the table. \param app
   * Application that needs access to a resource. \param permission Permission
   * assigned to app could be 'yes','no' or 'ask'. \param callback Callback
   * function for async operations. \return String of Stored permission.
   */
  void SetPermission(const std::string& table,
                     const std::string& id,
                     const std::string& app,
                     const std::vector<std::string>& permission,
                     const std::function<void(bool ready)>& callback) const;

  /**
   * \brief Retrieves permission for app.
   * \param table Table of resources for apps which could be either a device or
   * a feature. \param id Identifier for the resource in the table. \param app
   * Application that needs access to a resource. \param callback Callback
   * function for async operations. \return String of Stored permission.
   */
  void GetPermission(
      const std::string& table,
      const std::string& id,
      const std::string& app,
      const std::function<void(PermissionStatus status,
                               std::vector<std::string> permissions)>& callback)
      const;

  /**
   * \brief Delete permission for app.
   * \param table Table of resources for apps which could be either a device or
   * a feature. \param id Identifier for the resource in the table. \param app
   * Application that needs access to a resource. \param callback Callback
   * function for async operations. \return String of Stored permission.
   */
  void DeletePermission(const std::string& table,
                        const std::string& id,
                        const std::string& app,
                        const std::function<void(bool ready)>& callback) const;

  /**
   * \brief Retrieves all permissions for a specific resource.
   * \param table Table of resources for apps which could be either a device or
   * a feature. \param id Identifier for the resource in the table. \param app
   * Application that needs access to a resource \param callback Callback
   * function for async operations. \return Map of Stored permissions to a
   * resource.
   */
  void GetAllPermissions(
      const std::string& table,
      const std::string& id,
      const std::string& app,
      const std::function<void(PermissionStatus status)>& callback) const;

  /**
   * \brief Sets an entire resource entry with multiple app permissions at once.
   * \param table Table of resources for apps which could be either a device or
   * a feature.
   * \param id Identifier for the resource in the table.
   * \param permissions Permissions assigned to apps in a resource could be
   * 'yes','no' or 'ask'. \param callback Callback function for async
   * operations.
   */
  void SetResource(
      const std::string& table,
      const std::string& id,
      const std::map<std::string, std::vector<std::string>>& permissions,
      const std::function<void(bool ready)>& callback) const;

  /**
   * \brief Delete an entire resource and ALL associated app permissions.
   * \param table Table of resources for apps which could be either a device or
   * a feature.
   * \param id Identifier for the resource in the table.
   * \param callback Callback function for async operations.
   */
  void DeleteResource(const std::string& table,
                      const std::string& id,
                      const std::function<void(bool ready)>& callback) const;

  /**
   * \brief Get all device types that have any permissions set.
   * \param table Table of resources for apps which could be either a device or
   * a feature. \param callback Callback function for async operations.
   */
  void GetAllResource(
      const std::string& table,
      const std::function<void(bool success,
                               std::vector<std::string> resources)>& callback)
      const;

  /**
   * \brief Removes all app permissions from all tables and resources.
   * \param app Application that needs to uninstall and removes it's resources.
   * \param callback Callback function for async operations.
   */
  void RemoveAllAppPermissions(
      const std::string& app,
      const std::function<void(bool ready)>& callback) const;

  /**
   * \brief Storing timestamps of when a resource was last accessed.
   * \param table Table of resources for apps which could be either a device or
   * a feature. \param id Identifier for the resource in the table. \param data
   * A human-readable descriptions, or storing state information that multiple
   * apps might need to know about the resource. \param callback Callback
   * function for async operations.
   */
  void StoreTimestamp(const std::string& table,
                      const std::string& id,
                      const std::string& data,
                      const std::function<void(bool ready)>& callback) const;

 private:
  asio::io_context& io_context_;
  plugin_common_sdbus::SessionDBus& session_bus_;
};
}  // namespace flatpak_plugin
#endif  // IVI_HOMESCREEN_ACCESS_PORTAL_H