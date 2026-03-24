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

#include "permissions_portal.h"

#include <iomanip>
#include "../../flatpak_shim.h"
#include "asio/post.hpp"
#include "spdlog/spdlog.h"

namespace flatpak_plugin {

PermissionsPortal::PermissionsPortal(asio::io_context& io_context)
    : io_context_(io_context),
      session_bus_(plugin_common_sdbus::SessionDBus::Instance()) {}

void PermissionsPortal::CheckAllPermissions(
    const std::string& app,
    const std::vector<std::string>& permissions,
    const std::function<void(std::map<std::string, PermissionStatus>)>&
        callback) const {
  if (permissions.empty()) {
    spdlog::error("[AccessPortal] CheckAllPermissions: empty");
    callback({});
    return;
  }

  auto results = std::make_shared<std::map<std::string, PermissionStatus>>();
  auto counter = std::make_shared<std::atomic<int>>(0);
  auto total = std::make_shared<int>(static_cast<int>(permissions.size()));
  auto callback_wrapper = std::make_shared<
      std::function<void(std::map<std::string, PermissionStatus>)>>(callback);

  for (const auto& permission : permissions) {
    std::string table = "devices";
    std::string resource_id = permission;
    if (permission == "location") {
      table = "location";
      resource_id = "location";
    } else if (permission == "notifications") {
      table = "notifications";
      resource_id = "notifications";
    } else if (permission == "background") {
      table = "background";
      resource_id = app;
    }

    GetAllPermissions(
        table, resource_id, app,
        [this, results, callback_wrapper, counter, total,
         permission](PermissionStatus status) {
          asio::post(io_context_, [results, callback_wrapper, counter, total,
                                   permission, status]() {
            (*results)[permission] = status;
            int current = counter->fetch_add(1, std::memory_order_relaxed) + 1;

            if (current == *total) {
              (*callback_wrapper)(*results);
            }
          });
        });
  }
}

void PermissionsPortal::GetPermission(
    const std::string& table,
    const std::string& id,
    const std::string& app,
    const std::function<void(PermissionStatus status,
                             std::vector<std::string> permissions)>& callback)
    const {
  std::vector<std::string> permissions;
  spdlog::debug("[AccessPortal] GetPermission table={}, id={}, app={}", table,
                id, app);
  try {
    auto& conn = session_bus_.GetConnection();
    auto proxy =
        std::make_shared<std::unique_ptr<sdbus::IProxy>>(sdbus::createProxy(
            conn,
            sdbus::ServiceName{"org.freedesktop.impl.portal.PermissionStore"},
            sdbus::ObjectPath{"/org/freedesktop/impl/portal/PermissionStore"}));

    (*proxy)
        ->callMethodAsync("GetPermission")
        .onInterface("org.freedesktop.impl.portal.PermissionStore")
        .withArguments(table, id, app)
        .uponReplyInvoke(
            [this, callback, table, id, app,
             proxy](  // proxy captured to extend lifetime
                std::optional<sdbus::Error>
                    error,  // NOLINT(performance-unnecessary-value-param)
                const std::vector<std::string>& permissions) {
              asio::post(io_context_, [callback, error, permissions, table, id,
                                       app]() {
                if (error) {
                  const std::string error_msg = error->getMessage();
                  if (error_msg.find("No entry") != std::string::npos ||
                      error_msg.find("not found") != std::string::npos ||
                      error->getName() ==
                          "org.freedesktop.DBus.Error.UnknownMethod" ||
                      error->getName() ==
                          "org.freedesktop.portal.Error.NotFound") {
                    spdlog::debug(
                        "[AccessPortal] Permission not set for {}/{}/{}", table,
                        id, app);
                    callback(PermissionStatus::NOT_SET, {});
                    return;
                  }
                  spdlog::error("[AccessPortal] GetPermission failed: {} ({})",
                                error->getMessage(), error->getName());
                  callback(PermissionStatus::NOT_SET, {});
                  return;
                }

                // success to retrieve
                if (permissions.empty()) {
                  callback(PermissionStatus::NOT_SET, {});
                } else if (permissions[0] == "yes") {
                  callback(PermissionStatus::GRANTED, permissions);
                } else if (permissions[0] == "no") {
                  callback(PermissionStatus::DENIED, permissions);
                } else if (permissions[0] == "ask") {
                  callback(PermissionStatus::ASK, permissions);
                } else {
                  spdlog::warn("[AccessPortal] Unknown permission value: {}",
                               permissions[0]);
                  callback(PermissionStatus::NOT_SET, permissions);
                }
              });
            });
  } catch (const std::exception& e) {
    spdlog::error("[AccessPortal] Exception: {}", e.what());
    callback(PermissionStatus::NOT_SET, {});
  }
}

void PermissionsPortal::SetPermission(
    const std::string& table,
    const std::string& id,
    const std::string& app,
    const std::vector<std::string>& permission,
    const std::function<void(bool ready)>& callback) const {
  spdlog::debug(
      "[AccessPortal] SetPermission: table={}, id={}, app={}, perm={}", table,
      id, app, permission.empty() ? "empty" : permission[0]);
  try {
    auto& conn = session_bus_.GetConnection();
    auto proxy =
        std::make_shared<std::unique_ptr<sdbus::IProxy>>(sdbus::createProxy(
            conn,
            sdbus::ServiceName{"org.freedesktop.impl.portal.PermissionStore"},
            sdbus::ObjectPath{"/org/freedesktop/impl/portal/PermissionStore"}));
    (*proxy)
        ->callMethodAsync("SetPermission")
        .onInterface("org.freedesktop.impl.portal.PermissionStore")
        .withArguments(table, true, id, app, permission)
        .uponReplyInvoke(
            [callback, proxy]  // proxy captured to extend lifetime
            (std::optional<sdbus::Error>
                 error) {  // NOLINT(performance-unnecessary-value-param)
              spdlog::debug(
                  "[AccessPortal] SetPermission callback received: error={}",
                  error.has_value());

              if (error) {
                spdlog::error("[AccessPortal] SetPermission failed: {}",
                              error->getMessage());
                callback(false);
              } else {
                spdlog::debug("[AccessPortal] SetPermission successful");
                callback(true);
              }
            });

    spdlog::debug("[AccessPortal] SetPermission async call started");

  } catch (const std::exception& e) {
    spdlog::error("[AccessPortal] SetPermission exception: {}", e.what());
    callback(false);
  }
}

void PermissionsPortal::DeletePermission(
    const std::string& table,
    const std::string& id,
    const std::string& app,
    const std::function<void(bool ready)>& callback) const {
  spdlog::debug("[AccessPortal] DeletePermission: table={}, id={},app={}",
                table, id, app);
  try {
    auto& conn = session_bus_.GetConnection();
    auto proxy =
        std::make_shared<std::unique_ptr<sdbus::IProxy>>(sdbus::createProxy(
            conn,
            sdbus::ServiceName{"org.freedesktop.impl.portal.PermissionStore"},
            sdbus::ObjectPath{"/org/freedesktop/impl/portal/PermissionStore"}));

    (*proxy)
        ->callMethodAsync("DeletePermission")
        .onInterface("org.freedesktop.impl.portal.PermissionStore")
        .withArguments(table, id, app)
        .uponReplyInvoke(
            [this, callback, proxy]  // proxy captured to extend lifetime
            (std::optional<sdbus::Error>
                 error) {  // NOLINT(performance-unnecessary-value-param)
              asio::post(io_context_, [callback, error]() {
                if (error) {
                  const auto& msg = error->getMessage();
                  if (msg.find("No entry") != std::string::npos ||
                      msg.find("not found") != std::string::npos) {
                    spdlog::debug(
                        "[AccessPortal] DeletePermission: no entry, skipping");
                    callback(true);
                    return;
                  }
                  spdlog::error("[AccessPortal] DeletePermission failed: {}",
                                msg);
                  callback(false);
                } else {
                  callback(true);
                }
              });
            });
  } catch (const std::exception& e) {
    spdlog::error("[AccessPortal] Exception: {}", e.what());
    callback(false);
  }
}

void PermissionsPortal::SetResource(
    const std::string& table,
    const std::string& id,
    const std::map<std::string, std::vector<std::string>>& permissions,
    const std::function<void(bool ready)>& callback) const {
  auto now = std::chrono::system_clock::now();
  auto now_c = std::chrono::system_clock::to_time_t(now);
  std::ostringstream oss;
  oss << std::put_time(std::localtime(&now_c), "%F %T");
  std::string log = oss.str();
  spdlog::debug("[AccessPortal] SetResource: table={}, id={}", table, id);
  try {
    auto& conn = session_bus_.GetConnection();
    auto proxy =
        std::make_shared<std::unique_ptr<sdbus::IProxy>>(sdbus::createProxy(
            conn,
            sdbus::ServiceName{"org.freedesktop.impl.portal.PermissionStore"},
            sdbus::ObjectPath{"/org/freedesktop/impl/portal/PermissionStore"}));

    (*proxy)
        ->callMethodAsync("Set")
        .onInterface("org.freedesktop.impl.portal.PermissionStore")
        .withArguments(table, true, id, permissions, log)
        .uponReplyInvoke(
            [this, callback, proxy]  // proxy captured to extend lifetime
            (std::optional<sdbus::Error>
                 error) {  // NOLINT(performance-unnecessary-value-param)
              asio::post(io_context_, [callback, error]() {
                if (error) {
                  spdlog::error("[AccessPortal] Set Resource failed: {}",
                                error->getMessage());
                  callback(false);
                } else {
                  spdlog::debug("Set Resource successfully");
                  callback(true);
                }
              });
            });
  } catch (const std::exception& e) {
    spdlog::error("[AccessPortal] Exception: {}", e.what());
    callback(false);
  }
}

void PermissionsPortal::DeleteResource(
    const std::string& table,
    const std::string& id,
    const std::function<void(bool ready)>& callback) const {
  spdlog::debug("[AccessPortal] DeleteResource: table={}, id={}", table, id);
  try {
    auto& conn = session_bus_.GetConnection();
    auto proxy =
        std::make_shared<std::unique_ptr<sdbus::IProxy>>(sdbus::createProxy(
            conn,
            sdbus::ServiceName{"org.freedesktop.impl.portal.PermissionStore"},
            sdbus::ObjectPath{"/org/freedesktop/impl/portal/PermissionStore"}));

    (*proxy)
        ->callMethodAsync("Delete")
        .onInterface("org.freedesktop.impl.portal.PermissionStore")
        .withArguments(table, id)
        .uponReplyInvoke(
            [this, callback, proxy]  // proxy captured to extend lifetime
            (std::optional<sdbus::Error>
                 error) {  // NOLINT(performance-unnecessary-value-param)
              asio::post(io_context_, [callback, error]() {
                if (error) {
                  spdlog::error("[AccessPortal] Delete Resource failed: {}",
                                error->getMessage());
                  callback(false);
                } else {
                  spdlog::debug("Delete Resource successfully");
                  callback(true);
                }
              });
            });
  } catch (const std::exception& e) {
    spdlog::error("[AccessPortal] Exception: {}", e.what());
    callback(false);
  }
}
void PermissionsPortal::GetAllResource(
    const std::string& table,
    const std::function<void(bool success, std::vector<std::string> resources)>&
        callback) const {
  spdlog::debug("[AccessPortal] GetAllResources: table={}", table);
  try {
    auto& conn = session_bus_.GetConnection();
    auto proxy =
        std::make_shared<std::unique_ptr<sdbus::IProxy>>(sdbus::createProxy(
            conn,
            sdbus::ServiceName{"org.freedesktop.impl.portal.PermissionStore"},
            sdbus::ObjectPath{"/org/freedesktop/impl/portal/PermissionStore"}));

    (*proxy)
        ->callMethodAsync("List")
        .onInterface("org.freedesktop.impl.portal.PermissionStore")
        .withArguments(table)
        .uponReplyInvoke(
            [this, callback, proxy]  // proxy captured to extend lifetime
            (std::optional<sdbus::Error>
                 error,  // NOLINT(performance-unnecessary-value-param)
             const std::vector<std::string>& resources) {
              asio::post(io_context_, [callback, error, resources]() {
                if (error) {
                  spdlog::error("[AccessPortal] Get all Resource failed: {}",
                                error->getMessage());
                  callback(false, resources);
                } else {
                  spdlog::debug(
                      "Get all Resource successfully fetched {} resource",
                      resources.size());
                  callback(true, resources);
                }
              });
            });

  } catch (const std::exception& e) {
    spdlog::error("[AccessPortal] Exception: {}", e.what());
    callback(false, {});
  }
}

void PermissionsPortal::GetAllPermissions(
    const std::string& table,
    const std::string& id,
    const std::string& app,
    const std::function<void(PermissionStatus status)>& callback) const {
  spdlog::debug("[AccessPortal] GetAllPermissions: table={}, id={}, app={}",
                table, id, app);

  try {
    auto& conn = session_bus_.GetConnection();
    auto proxy =
        std::make_shared<std::unique_ptr<sdbus::IProxy>>(sdbus::createProxy(
            conn,
            sdbus::ServiceName{"org.freedesktop.impl.portal.PermissionStore"},
            sdbus::ObjectPath{"/org/freedesktop/impl/portal/PermissionStore"}));
    (*proxy)
        ->callMethodAsync("Lookup")
        .onInterface("org.freedesktop.impl.portal.PermissionStore")
        .withArguments(table, id)
        .uponReplyInvoke(
            [callback, app, table, id,
             proxy]  // proxy captured to extend lifetime
            (std::optional<sdbus::Error>
                 error,  // NOLINT(performance-unnecessary-value-param)
             const std::map<std::string, std::vector<std::string>>&
                 permissions) {
              spdlog::debug(
                  "[AccessPortal] Callback received: table={}, id={}, error={}",
                  table, id, error.has_value());

              if (error) {
                // Handle "not found" as NOT_SET
                if (error->getMessage().find("No table") != std::string::npos ||
                    error->getMessage().find("not found") !=
                        std::string::npos) {
                  callback(PermissionStatus::NOT_SET);
                  return;
                }
                spdlog::error("[AccessPortal] Error: {}", error->getMessage());
                callback(PermissionStatus::NOT_SET);
                return;
              }

              auto item = permissions.find(app);
              if (item == permissions.end() || item->second.empty()) {
                callback(PermissionStatus::NOT_SET);
                return;
              }

              const std::string& perm = item->second[0];
              if (perm == "yes")
                callback(PermissionStatus::GRANTED);
              else if (perm == "no")
                callback(PermissionStatus::DENIED);
              else if (perm == "ask")
                callback(PermissionStatus::ASK);
              else
                callback(PermissionStatus::NOT_SET);
            });
  } catch (const std::exception& e) {
    spdlog::error("[AccessPortal] Exception: {}", e.what());
    callback(PermissionStatus::NOT_SET);
  }
}

void PermissionsPortal::RemoveAllAppPermissions(
    const std::string& app,
    const std::function<void(bool ready)>& callback) const {
  struct TableEntry {
    std::string table;
    std::string resource_id;
  };

  auto fixed_entries =
      std::make_shared<std::vector<TableEntry>>(std::vector<TableEntry>{
          {"notifications", "notifications"},
          {"location", "location"},
          {"background", app},  // resource_id == app for background
      });

  GetAllResource("devices", [this, app, fixed_entries, callback](
                                bool /*ready*/,
                                const std::vector<std::string>& resources) {
    auto table_entries =
        std::make_shared<std::vector<TableEntry>>(*fixed_entries);
    for (const auto& entry : resources) {
      table_entries->push_back({"devices", entry});
    }

    auto total = std::make_shared<int>(table_entries->size());
    auto counter = std::make_shared<std::atomic<int>>(0);
    auto any_error = std::make_shared<bool>(false);

    if (table_entries->empty()) {
      callback(true);
      return;
    }

    for (const auto& entry : *table_entries) {
      DeletePermission(
          entry.table, entry.resource_id, app,
          [counter, total, any_error, callback](bool ok) {
            if (!ok) {
              *any_error = true;
            }
            int current = counter->fetch_add(1, std::memory_order_relaxed) + 1;
            if (current == *total) {
              callback(!*any_error);
            }
          });
    }
  });
}

void PermissionsPortal::StoreTimestamp(
    const std::string& table,
    const std::string& id,
    const std::string& data,
    const std::function<void(bool ready)>& callback) const {
  spdlog::debug("[AccessPortal] StoreTimestamp: table={}, id={}, data={}",
                table, id, data);
  try {
    auto& conn = session_bus_.GetConnection();
    auto proxy =
        std::make_shared<std::unique_ptr<sdbus::IProxy>>(sdbus::createProxy(
            conn,
            sdbus::ServiceName{"org.freedesktop.impl.portal.PermissionStore"},
            sdbus::ObjectPath{"/org/freedesktop/impl/portal/PermissionStore"}));

    (*proxy)
        ->callMethodAsync("SetValue")
        .onInterface("org.freedesktop.impl.portal.PermissionStore")
        .withArguments(table, true, id, data)
        .uponReplyInvoke(
            [this, callback, proxy]  // proxy captured to extend lifetime
            (std::optional<sdbus::Error>
                 error) {  // NOLINT(performance-unnecessary-value-param)
              asio::post(io_context_, [callback, error]() {
                if (error) {
                  spdlog::error("[AccessPortal] StoreTimestamp failed: {}",
                                error->getMessage());
                  callback(false);
                } else {
                  spdlog::debug("[AccessPortal] SetPermission successful");
                  callback(true);
                }
              });
            });
  } catch (const std::exception& e) {
    spdlog::error("[AccessPortal] Exception: {}", e.what());
    callback(false);
  }
}

}  // namespace flatpak_plugin