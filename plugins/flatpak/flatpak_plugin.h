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

#include <flatpak/flatpak.h>

#include <filesystem>
#include <future>
#include <thread>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar.h>
#include <asio/io_context_strand.hpp>

#include "messages.h"

namespace flatpak_plugin {

class FlatpakPlugin final : public flutter::Plugin, public FlatpakApi {
 public:
  struct version {
    uint32_t major;
    uint32_t minor;
    uint32_t micro;
  };

  struct installation {
    std::string id;
    std::string display_name;
    std::filesystem::path path;
    bool no_interaction;
    bool is_user;
    int32_t priority;
    std::vector<std::string> default_languages;
    std::vector<std::string> default_locales;
  };

  struct remote {
    std::string name;
    std::filesystem::path appstream_dir;
    std::filesystem::path appstream_timestamp;
    std::string url;
    std::string collection_id;
    std::string title;
    std::string comment;
    std::string description;
    std::string homepage;
    std::string icon;
    std::string default_branch;
    std::string main_ref;
    bool gpg_verify;
    bool no_enumerate;
    bool no_deps;
    bool disabled;
    int32_t prio;
    std::string filter;
  };

  struct desktop_file {
    std::string name;
    std::string comment;
    std::string exec;
    std::string icon;
    bool terminal;
    std::string type;
    bool startupNotify;
    std::string categories;
    std::string keywords;
    std::string dbus_activatable;
  };

  static void RegisterWithRegistrar(flutter::PluginRegistrar* registrar);

  FlatpakPlugin();

  ~FlatpakPlugin() override;

  static flutter::EncodableList GetRemotes(FlatpakInstallation* installation,
                                           const std::string& default_arch);

  static flutter::EncodableList GetApplicationList(
      FlatpakInstallation* installation);

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

  version version_{};
  std::string default_arch_;
  std::vector<std::string> supported_arches_;
  std::vector<std::unique_ptr<struct installation>> installations_;
  std::mutex installations_mutex_;

  static void PrintInstallation(
      const std::unique_ptr<struct installation>& install);

  static void process_system_installation(gpointer data, gpointer user_data);

  std::future<void> GetInstallations();

  static std::string get_application_id(FlatpakInstalledRef* installed_ref);

  static void parse_appstream_xml(FlatpakInstalledRef* installed_ref,
                                  const char* deploy_dir,
                                  bool print_raw_xml = false);

  static void parse_repo_appstream_xml(const char* appstream_xml);

  static void parse_desktop_file(const std::filesystem::path& filepath,
                                 struct desktop_file& desktop);
};
}  // namespace flatpak_plugin

#endif  // FLUTTER_PLUGIN_FLATPAK_PLUGIN_H