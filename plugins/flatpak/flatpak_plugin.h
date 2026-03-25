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

#ifndef FLUTTER_PLUGIN_FLATPAK_PLUGIN_H
#define FLUTTER_PLUGIN_FLATPAK_PLUGIN_H

#include <thread>

#include <flutter/plugin_registrar.h>
#include <asio/io_context_strand.hpp>

#include "cache/cache_manager.h"
#include "flatpak_shim.h"
#include "messages.g.h"
#include "method_channel.h"
#include "portals/portal_manager.h"

namespace flatpak_plugin {
class FlatpakPlugin final : public flutter::Plugin, public FlatpakApi {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrar* registrar);

  explicit FlatpakPlugin(flutter::PluginRegistrar* registrar);

  ~FlatpakPlugin() override;

  void Init();

  // Get Flatpak Version
  ErrorOr<std::string> GetVersion() override;

  static ErrorOr<flutter::EncodableList> GetRemotesByInstallationId(
      const std::string& installation_id);

  // Get the default flatpak arch
  ErrorOr<std::string> GetDefaultArch() override;

  // Get all arches supported by flatpak
  ErrorOr<flutter::EncodableList> GetSupportedArches() override;

  // Get configuration of all remote repositories.
  ErrorOr<flutter::EncodableList> GetSystemInstallations() override;

  // Get configuration of user installation.
  ErrorOr<Installation> GetUserInstallation() override;

  // Add a remote repository.
  ErrorOr<bool> RemoteAdd(const Remote& configuration) override;

  // Remove Remote configuration.
  ErrorOr<bool> RemoteRemove(const std::string& id) override;

  // Get a list of applications installed on machine.
  ErrorOr<flutter::EncodableList> GetApplicationsInstalled() override;

  // Get a list of applications needing update on machine.
  ErrorOr<flutter::EncodableList> GetApplicationsUpdate() override;

  // Get list of applications hosted on a remote.
  ErrorOr<flutter::EncodableList> GetApplicationsRemote(
      const std::string& id) override;

  // Install application of given id.
  void ApplicationInstall(
      const std::string& id,
      std::function<void(ErrorOr<bool> reply)> result) override;

  // Uninstall application with specified id.
  void ApplicationUninstall(
      const std::string& id,
      std::function<void(ErrorOr<bool> reply)> result) override;

  // Update application with specified id.
  void ApplicationUpdate(
      const std::string& id,
      std::function<void(ErrorOr<bool> reply)> result) override;

  // Start application using specified configuration.
  void ApplicationStart(
      const std::string& id,
      std::function<void(ErrorOr<bool> reply)> result) override;

  // Stop application with given id.
  ErrorOr<bool> ApplicationStop(const std::string& id) override;

  // Setup event channel to use before flatpak events.
  void SetupEventChannel(
      const std::string& app_id,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  // Disallow copy and assign.
  FlatpakPlugin(const FlatpakPlugin&) = delete;

  FlatpakPlugin& operator=(const FlatpakPlugin&) = delete;

 private:
  std::string name_;
  std::thread thread_;
  pthread_t pthread_self_;
  std::shared_ptr<asio::io_context> io_context_;
  asio::executor_work_guard<decltype(io_context_->get_executor())> work_;
  std::unique_ptr<asio::io_context::strand> strand_;
  std::unique_ptr<CacheManager> cache_manager_;
  std::shared_ptr<PortalManager> portal_manager_;
  std::shared_ptr<FlatpakShim> shim_;
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>>
      method_channel_;
  flutter::PluginRegistrar* registrar_;

  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
      const;
};
}  // namespace flatpak_plugin

#endif  // FLUTTER_PLUGIN_FLATPAK_PLUGIN_H
