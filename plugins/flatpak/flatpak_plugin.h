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

#define FLATPAK_EXTERN extern "C"
#include <flatpak/flatpak.h>

#include <filesystem>
#include <thread>

#include <flutter/plugin_registrar.h>
#include <asio/io_context_strand.hpp>

#include "messages.g.h"

namespace flatpak_plugin {
class FlatpakPlugin final : public flutter::Plugin, public FlatpakApi {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrar* registrar);

  FlatpakPlugin();

  ~FlatpakPlugin() override;

  // Get Flatpak Version
  ErrorOr<std::string> GetVersion() override;

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

  // Get list of applications hosted on a remote.
  ErrorOr<flutter::EncodableList> GetApplicationsRemote(
      const std::string& id) override;

  // Install application of given id.
  ErrorOr<bool> ApplicationInstall(const std::string& id) override;

  // Uninstall application with specified id.
  ErrorOr<bool> ApplicationUninstall(const std::string& id) override;

  // Start application using specified configuration.
  ErrorOr<bool> ApplicationStart(
      const std::string& id,
      const flutter::EncodableMap* configuration) override;

  // Stop application with given id.
  ErrorOr<bool> ApplicationStop(const std::string& id) override;

  static std::string FlatpakRemoteTypeToString(FlatpakRemoteType type) {
    switch (type) {
      case FLATPAK_REMOTE_TYPE_STATIC:
        // Statically configured remote
        return "Static";
      case FLATPAK_REMOTE_TYPE_USB:
        // Dynamically detected local pathname remote
        return "USB";
      case FLATPAK_REMOTE_TYPE_LAN:
        // Dynamically detected network remote
        return "LAN";
    }
    return "Unknown";
  }

  // Disallow copy and assign.
  FlatpakPlugin(const FlatpakPlugin&) = delete;

  FlatpakPlugin& operator=(const FlatpakPlugin&) = delete;

 private:
  std::string name_;
  std::thread thread_;
  pthread_t pthread_self_;
  std::unique_ptr<asio::io_context> io_context_;
  asio::executor_work_guard<decltype(io_context_->get_executor())> work_;
  std::unique_ptr<asio::io_context::strand> strand_;

  static GPtrArray* get_system_installations();

  static GPtrArray* get_remotes(FlatpakInstallation* installation);

  static std::vector<char> decompress_gzip(
      const std::vector<char>& compressedData,
      std::vector<char>& decompressedData);

  static std::time_t get_appstream_timestamp(
      const std::filesystem::path& timestamp_filepath);

  static Installation get_installation(FlatpakInstallation* installation);

  static flutter::EncodableList installation_get_default_languages(
      FlatpakInstallation* installation);

  static flutter::EncodableList installation_get_default_locales(
      FlatpakInstallation* installation);

  static std::string get_metadata_as_string(FlatpakInstalledRef* installed_ref);

  static std::string get_appdata_as_string(FlatpakInstalledRef* installed_ref);

  static void get_application_list(FlatpakInstallation* installation,
                                   flutter::EncodableList& application_list);

  static flutter::EncodableMap get_content_rating_map(FlatpakInstalledRef* ref);
};
}  // namespace flatpak_plugin

#endif  // FLUTTER_PLUGIN_FLATPAK_PLUGIN_H
