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

#include "flatpak_shim.h"

#include <filesystem>

#include <cstdlib>

#include <libxml/tree.h>
#include <libxml/xmlstring.h>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <sys/wait.h>
#include <zlib.h>

#include <fstream>

#include "plugins/common/string/string_tools.h"
#include "tools/logging.h"

#include <flutter/event_channel.h>
#include <flutter/event_stream_handler.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/standard_method_codec.h>
#include <asio/dispatch.hpp>
#include "appstream_catalog.h"
#include "asio/bind_executor.hpp"
#include "asio/post.hpp"
#include "component.h"
#include "cxxopts/include/cxxopts.hpp"
#include "messages.g.h"
#include "plugins/flatpak/flatpak_plugin.h"
#include "plugins/flatpak/operation_tracker.h"
#include "portals/portal_manager.h"
#include "screenshot.h"

namespace flatpak_plugin {

std::mutex FlatpakShim::monitor_mutex_;
std::map<std::string, std::shared_ptr<FlatpakShim::MonitorSession>>
    FlatpakShim::active_sessions_;

std::optional<std::string> FlatpakShim::getOptionalAttribute(
    const xmlNode* node,
    const char* attrName) {
  if (xmlChar* xmlValue =
          xmlGetProp(node, reinterpret_cast<const xmlChar*>(attrName))) {
    std::string value(reinterpret_cast<const char*>(xmlValue));
    xmlFree(xmlValue);
    return value;
  }
  return std::nullopt;
}

std::string FlatpakShim::getAttribute(const xmlNode* node,
                                      const char* attrName) {
  if (xmlChar* xmlValue =
          xmlGetProp(node, reinterpret_cast<const xmlChar*>(attrName))) {
    std::string value(reinterpret_cast<const char*>(xmlValue));
    xmlFree(xmlValue);
    return value;
  }
  return "";
}

void FlatpakShim::PrintComponent(const Component& component) {
  spdlog::info("[FlatpakPlugin] Component [{}]", component.getId());
  spdlog::info("[FlatpakPlugin] \tName: {}", component.getName());
  spdlog::info("[FlatpakPlugin] \tPackage Name: {}", component.getPkgName());
  spdlog::info("[FlatpakPlugin] \tSummary: {}", component.getSummary());

  if (component.getReleases().has_value()) {
    spdlog::info("[FlatpakPlugin] \tReleases: ");
    for (const auto& release : component.getReleases().value()) {
      spdlog::info("[FlatpakPlugin] \t\tVersion: {}", release.getVersion());
      spdlog::info("[FlatpakPlugin] \t\tTimestamp: {}", release.getTimestamp());
      if (release.getDescription().has_value()) {
        spdlog::info("[FlatpakPlugin] \t\tDescription: {}",
                     release.getDescription().value());
      }
      if (release.getSize().has_value()) {
        spdlog::info("[FlatpakPlugin] \t\tSize: {}", release.getSize().value());
      }
    }
  }

  // Checking and printing optional fields
  if (component.getVersion().has_value()) {
    spdlog::info("[FlatpakPlugin] \tVersion: {}",
                 component.getVersion().value());
  }
  if (component.getOrigin().has_value()) {
    spdlog::info("[FlatpakPlugin] \tOrigin: {}", component.getOrigin().value());
  }
  if (component.getMediaBaseurl().has_value()) {
    spdlog::info("[FlatpakPlugin] \tMedia Base URL: {}",
                 component.getMediaBaseurl().value());
  }
  if (component.getArchitecture().has_value()) {
    spdlog::info("[FlatpakPlugin] \tArchitecture: {}",
                 component.getArchitecture().value());
  }
  if (component.getProjectLicense().has_value()) {
    spdlog::info("[FlatpakPlugin] \tProject License: {}",
                 component.getProjectLicense().value());
  }
  if (component.getDescription().has_value()) {
    spdlog::info("[FlatpakPlugin] \tDescription: {}",
                 component.getDescription().value());
  }
  if (component.getUrl().has_value()) {
    spdlog::info("[FlatpakPlugin] \tURL: {}", component.getUrl().value());
  }
  if (component.getProjectGroup().has_value()) {
    spdlog::info("[FlatpakPlugin] \tProject Group: {}",
                 component.getProjectGroup().value());
  }
  if (component.getIcons().has_value()) {
    spdlog::info("[FlatpakPlugin] \tIcons:");
    for (const auto& icon : component.getIcons().value()) {
      icon.printIconDetails();
    }
  }
  if (component.getCategories().has_value()) {
    spdlog::info("[FlatpakPlugin] \tCategories:");
    for (const auto& category : component.getCategories().value()) {
      spdlog::info("[FlatpakPlugin] \t\t{}", category);
    }
  }
  if (component.getScreenshots().has_value()) {
    for (const auto& screenshot : component.getScreenshots().value()) {
      screenshot.printScreenshotDetails();
    }
  }
  if (component.getKeywords().has_value()) {
    spdlog::info("[FlatpakPlugin] \tKeywords:");
    for (const auto& keyword : component.getKeywords().value()) {
      spdlog::info("[FlatpakPlugin] \t\t{}", keyword);
    }
  }
  // Additional optional fields
  if (component.getSourcePkgname().has_value()) {
    spdlog::info("[FlatpakPlugin] \tSource Pkgname: {}",
                 component.getSourcePkgname().value());
  }
  if (component.getBundle().has_value()) {
    spdlog::info("[FlatpakPlugin] \tBundle: {}", component.getBundle().value());
  }
  if (component.getContentRatingType().has_value()) {
    spdlog::info("[FlatpakPlugin] \tContent Rating Type: [{}]",
                 component.getContentRatingType().value());
  }
  if (component.getContentRating().has_value()) {
    if (!component.getContentRating().value().empty()) {
      spdlog::info("[FlatpakPlugin] \tContent Rating:");
      for (const auto& [key, value] : component.getContentRating().value()) {
        spdlog::info("[FlatpakPlugin] \t\t{} = {}", key,
                     Component::RatingValueToString(value));
      }
    }
  }
  if (component.getAgreement().has_value()) {
    spdlog::info("[FlatpakPlugin] \tAgreement: {}",
                 component.getAgreement().value());
  }
}

GPtrArray* FlatpakShim::get_system_installations() {
  GError* error = nullptr;
  const auto sys_installs = flatpak_get_system_installations(nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Error getting system installations: {}",
                  error->message);
    g_ptr_array_unref(sys_installs);
    g_clear_error(&error);
  }
  return sys_installs;
}

GPtrArray* FlatpakShim::get_remotes(FlatpakInstallation* installation) {
  GError* error = nullptr;
  const auto remotes =
      flatpak_installation_list_remotes(installation, nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Error listing remotes: {}", error->message);
    g_clear_error(&error);
  }
  return remotes;
}

std::time_t FlatpakShim::get_appstream_timestamp(
    const std::filesystem::path& timestamp_filepath) {
  if (timestamp_filepath.empty() || !exists(timestamp_filepath)) {
    spdlog::debug(
        "[FlatpakPlugin] appstream_timestamp path is empty or does not exist: "
        "{}",
        timestamp_filepath.string());
    return std::time(nullptr);  // Return current time as fallback
  }

  try {
    const auto fileTime = std::filesystem::last_write_time(timestamp_filepath);
    const auto sctp =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            fileTime - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sctp);
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Failed to get timestamp for {}: {}",
                  timestamp_filepath.string(), e.what());
    return std::time(nullptr);
  }
}

void FlatpakShim::update_appstream() {
  GError* error = nullptr;

  auto installation = flatpak_installation_new_user(nullptr, &error);
  if (!installation) {
    spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                  error->message);
    g_clear_error(&error);
    return;
  }

  auto remotes = get_remotes(installation);
  if (!remotes) {
    spdlog::error("[FlatpakPlugin] Failed to fetch any remotes");
    g_object_unref(installation);
    return;
  }

  const auto arch = flatpak_get_default_arch();

  for (guint i = 0; i < remotes->len; i++) {
    const auto remote =
        static_cast<FlatpakRemote*>(g_ptr_array_index(remotes, i));

    if (flatpak_remote_get_disabled(remote))
      continue;

    const auto remote_name = flatpak_remote_get_name(remote);
    gboolean changed = FALSE;

    flatpak_installation_update_appstream_sync(installation, remote_name, arch,
                                               &changed, nullptr, &error);

    if (error) {
      spdlog::error("[FlatpakPlugin] Can't update AppStream for {}",
                    remote_name);
      g_clear_error(&error);
      continue;
    }

    if (changed) {
      spdlog::debug("[FlatpakPlugin] AppStream updated for {}", remote_name);
    }
  }

  g_ptr_array_unref(remotes);
  g_object_unref(installation);
  spdlog::debug("[FlatpakPlugin] Background AppStream sync finished.");
}

void FlatpakShim::format_time_iso8601(const time_t raw_time,
                                      char* buffer,
                                      const size_t buffer_size) {
  if (!buffer || buffer_size < 32) {
    spdlog::error("[FlatpakPlugin] Invalid buffer for time formatting");
    return;
  }

  tm tm_info{};
  if (localtime_r(&raw_time, &tm_info) == nullptr) {
    spdlog::error("[FlatpakPlugin] Failed to convert time");
    strncpy(buffer, "1970-01-01T00:00:00+00:00", buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return;
  }

  strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%S", &tm_info);
  long timezone_offset = tm_info.tm_gmtoff;
  const char sign = (timezone_offset >= 0) ? '+' : '-';
  timezone_offset = std::abs(timezone_offset);

  size_t len = strlen(buffer);
  snprintf(buffer + len, buffer_size - len, "%c%02ld:%02ld", sign,
           timezone_offset / 3600, (timezone_offset % 3600) / 60);
}

flutter::EncodableList FlatpakShim::installation_get_default_languages(
    FlatpakInstallation* installation) {
  flutter::EncodableList languages;
  GError* error = nullptr;
  const auto default_languages =
      flatpak_installation_get_default_languages(installation, &error);

  if (error) {
    spdlog::error("[FlatpakPlugin] Error getting default languages: {}",
                  error->message);
    g_error_free(error);
    return languages;
  }

  if (default_languages) {
    for (auto language = default_languages; *language; ++language) {
      languages.emplace_back(static_cast<const char*>(*language));
    }
    g_strfreev(default_languages);
  }
  return languages;
}

flutter::EncodableList FlatpakShim::installation_get_default_locales(
    FlatpakInstallation* installation) {
  flutter::EncodableList locales;
  GError* error = nullptr;

  const auto default_locales =
      flatpak_installation_get_default_locales(installation, &error);
  if (error) {
    spdlog::error(
        "[FlatpakPlugin] flatpak_installation_get_default_locales: {}",
        error->message);
    g_error_free(error);
    error = nullptr;
  }
  if (default_locales != nullptr) {
    for (auto locale = default_locales; *locale != nullptr; ++locale) {
      locales.emplace_back(static_cast<const char*>(*locale));
    }
    g_strfreev(default_locales);
  }
  return locales;
}

std::string FlatpakShim::FlatpakRemoteTypeToString(
    const FlatpakRemoteType type) {
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

Installation FlatpakShim::get_installation(FlatpakInstallation* installation) {
  flutter::EncodableList remote_list;

  if (const auto remotes = get_remotes(installation)) {
    for (size_t j = 0; j < remotes->len; j++) {
      const auto remote =
          static_cast<FlatpakRemote*>(g_ptr_array_index(remotes, j));

      if (!remote) {
        spdlog::error("[FlatpakPlugin] Null remote at index {}", j);
        continue;
      }

      try {
        const auto name = flatpak_remote_get_name(remote);
        const auto url = flatpak_remote_get_url(remote);

        // Validate required fields before creating a Remote object
        if (!name || !url) {
          spdlog::error(
              "[FlatpakPlugin] Skipping remote with missing name or URL");
          continue;
        }

        const auto collection_id = flatpak_remote_get_collection_id(remote);
        const auto title = flatpak_remote_get_title(remote);
        const auto comment = flatpak_remote_get_comment(remote);
        const auto description = flatpak_remote_get_description(remote);
        const auto homepage = flatpak_remote_get_homepage(remote);
        const auto icon = flatpak_remote_get_icon(remote);
        const auto default_branch = flatpak_remote_get_default_branch(remote);
        const auto main_ref = flatpak_remote_get_main_ref(remote);
        const auto filter = flatpak_remote_get_filter(remote);
        const bool gpg_verify = flatpak_remote_get_gpg_verify(remote);
        const bool no_enumerate = flatpak_remote_get_noenumerate(remote);
        const bool no_deps = flatpak_remote_get_nodeps(remote);
        const bool disabled = flatpak_remote_get_disabled(remote);
        const int32_t prio = flatpak_remote_get_prio(remote);

        const auto default_arch = flatpak_get_default_arch();
        auto appstream_timestamp_path = g_file_get_path(
            flatpak_remote_get_appstream_timestamp(remote, default_arch));
        auto appstream_dir_path = g_file_get_path(
            flatpak_remote_get_appstream_dir(remote, default_arch));

        auto appstream_timestamp = get_appstream_timestamp(
            appstream_timestamp_path ? appstream_timestamp_path : "");
        char formatted_time[64] = {0};  // Initialize buffer
        format_time_iso8601(appstream_timestamp, formatted_time,
                            sizeof(formatted_time));

        remote_list.emplace_back(flutter::CustomEncodableValue(Remote(
            std::string(name), std::string(url),
            collection_id ? std::string(collection_id) : "",
            title ? std::string(title) : "",
            comment ? std::string(comment) : "",
            description ? std::string(description) : "",
            homepage ? std::string(homepage) : "",
            icon ? std::string(icon) : "",
            default_branch ? std::string(default_branch) : "",
            main_ref ? std::string(main_ref) : "",
            FlatpakRemoteTypeToString(flatpak_remote_get_remote_type(remote)),
            filter ? std::string(filter) : "", std::string(formatted_time),
            appstream_dir_path ? std::string(appstream_dir_path) : "",
            gpg_verify, no_enumerate, no_deps, disabled,
            static_cast<int64_t>(prio))));

        // Clean up allocated memory
        if (appstream_timestamp_path) {
          g_free(appstream_timestamp_path);
        }
        if (appstream_dir_path) {
          g_free(appstream_dir_path);
        }
      } catch (const std::exception& e) {
        spdlog::error("[FlatpakPlugin] Exception processing remote: {}",
                      e.what());
        continue;
      }
    }
    g_ptr_array_unref(remotes);
  }

  const auto id = flatpak_installation_get_id(installation);
  const auto display_name = flatpak_installation_get_display_name(installation);
  const auto installationPath = flatpak_installation_get_path(installation);
  const auto path = g_file_get_path(installationPath);
  const auto no_interaction =
      flatpak_installation_get_no_interaction(installation);
  const auto is_user = flatpak_installation_get_is_user(installation);
  const auto priority = flatpak_installation_get_priority(installation);
  const auto default_languages =
      installation_get_default_languages(installation);
  const auto default_locales = installation_get_default_locales(installation);

  // Validate required fields
  if (!id || !path) {
    spdlog::error("[FlatpakPlugin] Installation missing required fields");
    // Return a valid but minimal Installation object
    return Installation("unknown", "Unknown", "", false, false, 0,
                        flutter::EncodableList{}, flutter::EncodableList{},
                        flutter::EncodableList{});
  }

  // Clean up a path after use
  std::string path_str = path;
  g_free(const_cast<char*>(path));

  return Installation(std::string(id),
                      display_name ? std::string(display_name) : "", path_str,
                      no_interaction, is_user, static_cast<int64_t>(priority),
                      default_languages, default_locales, remote_list);
}

flutter::EncodableMap FlatpakShim::get_content_rating_map(
    FlatpakInstalledRef* ref) {
  flutter::EncodableMap result;
  const auto content_rating =
      flatpak_installed_ref_get_appdata_content_rating(ref);
  if (!content_rating) {
    return result;
  }

  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, content_rating);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    // Validate pointers before casting
    if (key && value) {
      const char* key_str = static_cast<const char*>(key);
      const char* value_str = static_cast<const char*>(value);

      if (key_str && value_str && strlen(key_str) > 0 &&
          strlen(value_str) > 0) {
        result[flutter::EncodableValue(std::string(key_str))] =
            flutter::EncodableValue(std::string(value_str));
      }
    }
  }
  return result;
}

void FlatpakShim::get_application_list(
    FlatpakInstallation* installation,
    flutter::EncodableList& application_list) {
  flutter::EncodableList result;
  GError* error = nullptr;

  const auto refs =
      flatpak_installation_list_installed_refs(installation, nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Error listing installed refs: {}",
                  error->message);
    g_clear_error(&error);
    return;
  }

  for (guint i = 0; i < refs->len; i++) {
    const auto ref =
        static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

    // Skip non-app refs
    if (flatpak_ref_get_kind(FLATPAK_REF(ref)) != FLATPAK_REF_KIND_APP) {
      continue;
    }

    const auto appdata_name = flatpak_installed_ref_get_appdata_name(ref);
    const auto ref_name = flatpak_ref_get_name(FLATPAK_REF(ref));
    const auto appdata_summary = flatpak_installed_ref_get_appdata_summary(ref);
    const auto appdata_version = flatpak_installed_ref_get_appdata_version(ref);
    const auto appdata_origin = flatpak_installed_ref_get_origin(ref);
    const auto appdata_license = flatpak_installed_ref_get_appdata_license(ref);
    const auto deploy_dir = flatpak_installed_ref_get_deploy_dir(ref);
    const auto content_rating_type =
        flatpak_installed_ref_get_appdata_content_rating_type(ref);
    const auto latest_commit = flatpak_installed_ref_get_latest_commit(ref);
    const auto eol = flatpak_installed_ref_get_eol(ref);
    const auto eol_rebase = flatpak_installed_ref_get_eol_rebase(ref);

    // Build full application ID
    const auto ref_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
    const auto ref_branch = flatpak_ref_get_branch(FLATPAK_REF(ref));
    std::string full_app_id = "app/";
    full_app_id += (ref_name ? ref_name : "");
    full_app_id += "/";
    full_app_id += (ref_arch ? ref_arch : "");
    full_app_id += "/";
    full_app_id += (ref_branch ? ref_branch : "");

    flutter::EncodableList subpath_list;
    if (const auto subpaths = flatpak_installed_ref_get_subpaths(ref);
        subpaths != nullptr) {
      for (auto sub_path = subpaths; *sub_path != nullptr; ++sub_path) {
        subpath_list.emplace_back(static_cast<const char*>(*sub_path));
      }
    }

    application_list.emplace_back(flutter::CustomEncodableValue(Application(
        appdata_name ? std::string(appdata_name)
                     : (ref_name ? std::string(ref_name) : ""),
        full_app_id, appdata_summary ? std::string(appdata_summary) : "",
        appdata_version ? std::string(appdata_version) : "",
        appdata_origin ? std::string(appdata_origin) : "",
        appdata_license ? std::string(appdata_license) : "",
        static_cast<int64_t>(flatpak_installed_ref_get_installed_size(ref)),
        deploy_dir ? std::string(deploy_dir) : "",
        flatpak_installed_ref_get_is_current(ref),
        content_rating_type ? std::string(content_rating_type) : "",
        get_content_rating_map(ref),
        latest_commit ? std::string(latest_commit) : "",
        eol ? std::string(eol) : "", eol_rebase ? std::string(eol_rebase) : "",
        subpath_list, get_metadata_as_string(ref),
        get_appdata_as_string(ref))));
  }
  g_ptr_array_unref(refs);
}

ErrorOr<flutter::EncodableList> FlatpakShim::GetApplicationsInstalled() {
  flutter::EncodableList application_list;
  GError* error = nullptr;
  auto installation = flatpak_installation_new_user(nullptr, &error);
  get_application_list(installation, application_list);

  const auto system_installations = get_system_installations();
  for (size_t i = 0; i < system_installations->len; i++) {
    installation = static_cast<FlatpakInstallation*>(
        g_ptr_array_index(system_installations, i));
    get_application_list(installation, application_list);
  }
  g_ptr_array_unref(system_installations);

  return ErrorOr<flutter::EncodableList>(std::move(application_list));
}

ErrorOr<flutter::EncodableList> FlatpakShim::GetApplicationsUpdate() {
  flutter::EncodableList application_list;
  GError* error = nullptr;
  auto system_installation = flatpak_installation_new_system(nullptr, &error);
  auto user_installation = flatpak_installation_new_user(nullptr, &error);
  const auto refs = flatpak_installation_list_installed_refs_for_update(
      system_installation, nullptr, &error);
  if (error) {
    spdlog::error(
        "[FlatpakPlugin] Error listing system installed refs for update: {}",
        error->message);
    g_clear_error(&error);
    return ErrorOr<flutter::EncodableList>(FlutterError(
        "INSTALLATION_ERROR", "Failed to get system installed refs"));
  }
  const auto user_refs = flatpak_installation_list_installed_refs_for_update(
      user_installation, nullptr, &error);

  if (error) {
    spdlog::error(
        "[FlatpakPlugin] Error listing user installed refs for update: {}",
        error->message);
    g_clear_error(&error);
    return ErrorOr<flutter::EncodableList>(FlutterError(
        "INSTALLATION_ERROR", "Failed to get user installed refs"));
  }
  g_ptr_array_extend_and_steal(refs, user_refs);

  for (guint i = 0; i < refs->len; i++) {
    const auto ref =
        static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

    // All refs will be added to list including runtimes
    const auto appdata_name = flatpak_installed_ref_get_appdata_name(ref);
    const auto ref_name = flatpak_ref_get_name(FLATPAK_REF(ref));
    const auto appdata_summary = flatpak_installed_ref_get_appdata_summary(ref);
    const auto appdata_version = flatpak_installed_ref_get_appdata_version(ref);
    const auto appdata_origin = flatpak_installed_ref_get_origin(ref);
    const auto appdata_license = flatpak_installed_ref_get_appdata_license(ref);
    const auto deploy_dir = flatpak_installed_ref_get_deploy_dir(ref);
    const auto content_rating_type =
        flatpak_installed_ref_get_appdata_content_rating_type(ref);
    const auto latest_commit = flatpak_installed_ref_get_latest_commit(ref);
    const auto eol = flatpak_installed_ref_get_eol(ref);
    const auto eol_rebase = flatpak_installed_ref_get_eol_rebase(ref);

    // Build full application ID
    const auto ref_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
    const auto ref_branch = flatpak_ref_get_branch(FLATPAK_REF(ref));
    std::string full_app_id = "app/";
    full_app_id += (ref_name ? ref_name : "");
    full_app_id += "/";
    full_app_id += (ref_arch ? ref_arch : "");
    full_app_id += "/";
    full_app_id += (ref_branch ? ref_branch : "");

    flutter::EncodableList subpath_list;
    if (const auto subpaths = flatpak_installed_ref_get_subpaths(ref);
        subpaths != nullptr) {
      for (auto sub_path = subpaths; *sub_path != nullptr; ++sub_path) {
        subpath_list.emplace_back(static_cast<const char*>(*sub_path));
      }
    }

    application_list.emplace_back(flutter::CustomEncodableValue(Application(
        appdata_name ? std::string(appdata_name)
                     : (ref_name ? std::string(ref_name) : ""),
        full_app_id, appdata_summary ? std::string(appdata_summary) : "",
        appdata_version ? std::string(appdata_version) : "",
        appdata_origin ? std::string(appdata_origin) : "",
        appdata_license ? std::string(appdata_license) : "",
        static_cast<int64_t>(flatpak_installed_ref_get_installed_size(ref)),
        deploy_dir ? std::string(deploy_dir) : "",
        flatpak_installed_ref_get_is_current(ref),
        content_rating_type ? std::string(content_rating_type) : "",
        get_content_rating_map(ref),
        latest_commit ? std::string(latest_commit) : "",
        eol ? std::string(eol) : "", eol_rebase ? std::string(eol_rebase) : "",
        subpath_list, get_metadata_as_string(ref),
        get_appdata_as_string(ref))));
  }
  g_ptr_array_unref(refs);
  return ErrorOr<flutter::EncodableList>(std::move(application_list));
}

ErrorOr<Installation> FlatpakShim::GetUserInstallation() {
  GError* error = nullptr;
  const auto installation = flatpak_installation_new_user(nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Error getting user installation: {}",
                  error->message);
    g_clear_error(&error);
    return ErrorOr<Installation>(
        FlutterError("INSTALLATION_ERROR", "Failed to get user installation"));
  }

  auto result = get_installation(installation);
  g_object_unref(installation);
  return ErrorOr<Installation>(std::move(result));
}

ErrorOr<flutter::EncodableList> FlatpakShim::GetSystemInstallations() {
  flutter::EncodableList installs_list;
  if (const auto system_installations = get_system_installations()) {
    for (size_t i = 0; i < system_installations->len; i++) {
      const auto installation = static_cast<FlatpakInstallation*>(
          g_ptr_array_index(system_installations, i));
      if (installation) {
        installs_list.emplace_back(
            flutter::CustomEncodableValue(get_installation(installation)));
      }
    }
    g_ptr_array_unref(system_installations);
  }
  return ErrorOr<flutter::EncodableList>(std::move(installs_list));
}

ErrorOr<flutter::EncodableList> FlatpakShim::GetApplicationsRemote(
    const std::string& id) {
  try {
    if (id.empty()) {
      return ErrorOr<flutter::EncodableList>(
          FlutterError("INVALID_REMOTE_GET", "Remote id is required"));
    }

    spdlog::info("[FlatpakPlugin] Get Applications from Remote {}", id);
    GError* error = nullptr;

    auto installation = flatpak_installation_new_user(nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                    error->message);
      g_clear_error(&error);
      return ErrorOr<flutter::EncodableList>(FlutterError(
          "INVALID_REMOTE_GET", "Failed to get user installation"));
    }
    const auto remote = flatpak_installation_get_remote_by_name(
        installation, id.c_str(), nullptr, &error);
    if (!remote) {
      spdlog::error("[FlatpakPlugin] Failed to get remote {}: {}", id.c_str(),
                    error->message);
      g_clear_error(&error);
      g_object_unref(installation);
      return ErrorOr(flutter::EncodableList{});
    }

    auto app_refs = flatpak_installation_list_remote_refs_sync(
        installation, id.c_str(), nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to get applications for remote: {}",
                    error->message);
      g_clear_error(&error);
      g_object_unref(installation);
      return ErrorOr<flutter::EncodableList>(FlutterError(
          "INVALID_REMOTE_GET", "Failed to get remote applications"));
    }

    if (!app_refs) {
      spdlog::error("[FlatpakPlugin] No applications found in remote {}", id);
      g_object_unref(installation);
      g_object_unref(remote);
      g_clear_error(&error);
      return ErrorOr(flutter::EncodableList{});
    }

    flutter::EncodableList application_list =
        convert_applications_to_EncodableList(app_refs, remote);

    g_ptr_array_unref(app_refs);
    g_object_unref(installation);
    spdlog::info("[FlatpakPlugin] Found {} applications in remote {} ",
                 application_list.size(), id);

    return ErrorOr<flutter::EncodableList>(std::move(application_list));
  } catch (const std::exception& e) {
    spdlog::error(
        "[FlatpakPlugin] Exception getting applications from remote: {}",
        e.what());
    return ErrorOr<flutter::EncodableList>(
        FlutterError("INVALID_REMOTE_GET", "Exception occurred"));
  }
}

ErrorOr<bool> FlatpakShim::RemoteAdd(const Remote& configuration) {
  try {
    if (configuration.name().empty() || configuration.url().empty()) {
      return ErrorOr<bool>(FlutterError("INVALID_REMOTE_ADD",
                                        "Remote name and URL are required"));
    }

    spdlog::info("[FlatpakPlugin] Adding Remote {}", configuration.name());
    GError* error = nullptr;
    auto installation = flatpak_installation_new_user(nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                    error->message);
      g_clear_error(&error);
      return ErrorOr<bool>(FlutterError("INVALID_REMOTE_ADD",
                                        "Failed to get user installation"));
    }

    // Check if a remote already exists
    auto existing_remote = flatpak_installation_get_remote_by_name(
        installation, configuration.name().c_str(), nullptr, nullptr);
    if (existing_remote) {
      spdlog::error("[FlatpakPlugin] Remote '{}' already exists",
                    configuration.name());
      g_object_unref(installation);
      g_object_unref(existing_remote);
      return ErrorOr<bool>(
          FlutterError("REMOTE_EXISTS", "Remote already exists"));
    }

    auto remote = flatpak_remote_new(configuration.name().c_str());
    if (!remote) {
      spdlog::error("[FlatpakPlugin] Failed to create remote object");
      g_object_unref(installation);
      return ErrorOr<bool>(
          FlutterError("INVALID_REMOTE_ADD", "Failed to create remote object"));
    }

    // Set required properties
    flatpak_remote_set_url(remote, configuration.url().c_str());

    // Set optional properties
    if (!configuration.title().empty()) {
      flatpak_remote_set_title(remote, configuration.title().c_str());
    }
    if (!configuration.collection_id().empty()) {
      flatpak_remote_set_collection_id(remote,
                                       configuration.collection_id().c_str());
    }
    if (!configuration.comment().empty()) {
      flatpak_remote_set_comment(remote, configuration.comment().c_str());
    }
    if (!configuration.description().empty()) {
      flatpak_remote_set_description(remote,
                                     configuration.description().c_str());
    }
    if (!configuration.default_branch().empty()) {
      flatpak_remote_set_default_branch(remote,
                                        configuration.default_branch().c_str());
    }
    if (!configuration.filter().empty()) {
      flatpak_remote_set_filter(remote, configuration.filter().c_str());
    }
    if (!configuration.homepage().empty()) {
      flatpak_remote_set_homepage(remote, configuration.homepage().c_str());
    }
    if (!configuration.icon().empty()) {
      flatpak_remote_set_icon(remote, configuration.icon().c_str());
    }
    if (!configuration.main_ref().empty()) {
      flatpak_remote_set_main_ref(remote, configuration.main_ref().c_str());
    }

    flatpak_remote_set_nodeps(remote, configuration.no_deps());
    flatpak_remote_set_gpg_verify(remote, configuration.gpg_verify());
    flatpak_remote_set_prio(remote, static_cast<int>(configuration.prio()));
    flatpak_remote_set_disabled(remote, configuration.disabled());

    gboolean result = flatpak_installation_add_remote(installation, remote,
                                                      TRUE, nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to add remote: {}", error->message);
      g_object_unref(installation);
      g_object_unref(remote);
      g_clear_error(&error);
      return ErrorOr<bool>(
          FlutterError("INVALID_REMOTE_ADD", "Failed to add remote"));
    }

    if (result) {
      spdlog::info("[FlatpakPlugin] Remote '{}' added successfully",
                   configuration.name());
    }

    g_object_unref(installation);
    g_object_unref(remote);
    return ErrorOr<bool>(static_cast<bool>(result));
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Exception adding remote: {}", e.what());
    return ErrorOr<bool>(
        FlutterError("INVALID_REMOTE_ADD", "Exception occurred"));
  }
}

ErrorOr<bool> FlatpakShim::RemoteRemove(const std::string& id) {
  try {
    if (id.empty()) {
      return ErrorOr<bool>(
          FlutterError("INVALID_REMOTE_REMOVE", "Remote ID is required"));
    }

    spdlog::info("[FlatpakPlugin] Removing remote {}", id);
    GError* error = nullptr;
    auto installation = flatpak_installation_new_user(nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                    error->message);
      g_clear_error(&error);
      return ErrorOr<bool>(FlutterError("INVALID_REMOTE_REMOVE",
                                        "Failed to get user installation"));
    }

    gboolean result = flatpak_installation_remove_remote(
        installation, id.c_str(), nullptr, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to remove remote: {}",
                    error->message);
      g_object_unref(installation);
      g_clear_error(&error);
      return ErrorOr<bool>(
          FlutterError("INVALID_REMOTE_REMOVE", "Failed to remove remote"));
    }

    if (result) {
      spdlog::info("[FlatpakPlugin] Remote '{}' removed successfully", id);
    }

    g_object_unref(installation);
    return ErrorOr<bool>(static_cast<bool>(result));
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Exception removing remote: {}", e.what());
    return ErrorOr<bool>(
        FlutterError("INVALID_REMOTE_REMOVE", "Exception occurred"));
  }
}

void FlatpakShim::ApplicationInstall(
    const std::string& id,
    const std::function<void(ErrorOr<bool>)>& callback) {
  if (id.empty()) {
    asio::post(*strand_, [callback]() {
      callback(ErrorOr<bool>(
          FlutterError("INVALID_APP_ID", "Application ID is required")));
    });
    return;
  }

  spdlog::info("[FlatpakPlugin] Installing application: {}", id);
  GError* error = nullptr;

  auto installation = flatpak_installation_new_user(nullptr, &error);
  if (error) {
    std::string err_msg = error->message;
    spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                  err_msg);
    g_clear_error(&error);
    asio::post(*strand_, [callback]() {
      callback(ErrorOr<bool>(FlutterError("INSTALLATION_ERROR",
                                          "Failed to get user installation")));
    });
    return;
  }

  GObjectGuard<FlatpakInstallation> installation_guard(installation);

  auto remote_and_ref = find_app_in_remotes(installation, id);
  if (remote_and_ref.first.empty()) {
    spdlog::error("[FlatpakPlugin] Application '{}' not found in any remote",
                  id);
    asio::post(*strand_, [callback]() {
      callback(ErrorOr<bool>(
          FlutterError("APP_NOT_FOUND", "Application not found in remotes")));
    });
    return;
  }

  auto found_ref = flatpak_ref_parse(remote_and_ref.second.c_str(), &error);
  if (error || !found_ref) {
    spdlog::error("[FlatpakPlugin] Failed to parse found ref: {}",
                  remote_and_ref.second);
    if (error) {
      g_clear_error(&error);
    }
    asio::post(*strand_, [callback]() {
      callback(ErrorOr<bool>(
          FlutterError("APP_NOT_FOUND", "Failed to parse found ref")));
    });
    return;
  }

  GObjectGuard<FlatpakRef> found_ref_guard(found_ref);

  spdlog::info("[FlatpakPlugin] Found app '{}' in remote '{}' as '{}'", id,
               remote_and_ref.first, remote_and_ref.second);

  auto remote_name = remote_and_ref.first;
  auto ref_name = remote_and_ref.second;

  // Check if the app is already installed
  auto installed_ref = flatpak_installation_get_installed_ref(
      installation, FLATPAK_REF_KIND_APP, flatpak_ref_get_name(found_ref),
      flatpak_ref_get_arch(found_ref), flatpak_ref_get_branch(found_ref),
      nullptr, &error);

  if (error && error->code != FLATPAK_ERROR_NOT_INSTALLED) {
    std::string err_msg = error->message;
    spdlog::error("[FlatpakPlugin] Error checking installed ref: {}", err_msg);
    g_clear_error(&error);
    asio::post(*strand_, [callback, err_msg]() {
      callback(ErrorOr<bool>(FlutterError("INSTALLATION_ERROR", err_msg)));
    });
    return;
  }
  if (error) {
    g_clear_error(&error);
  }

  if (installed_ref) {
    spdlog::info("[FlatpakPlugin] Application '{}' is already installed", id);
    g_object_unref(installed_ref);
    asio::post(*strand_, [callback]() { callback(ErrorOr<bool>(true)); });
    return;
  }

  spdlog::info("[FlatpakPlugin] Starting TWO-PHASE installation for: {}", id);

  // START QUEUE TRACKING
  operation_tracker_->TrackOperationStart(id, "install");

  FlatpakTransaction* transaction =
      flatpak_transaction_new_for_installation(installation, nullptr, &error);

  if (error) {
    std::string err_msg = error->message;
    spdlog::error("[FlatpakPlugin] Error creating transaction: {}", err_msg);
    g_clear_error(&error);
    asio::post(*strand_, [this, id, callback]() {
      operation_tracker_->SendOperationFinish(id, "install", false);
      callback(ErrorOr<bool>(
          FlutterError("TRANSACTION_ERROR", "Error creating transaction")));
    });
    return;
  }

  GObjectGuard<FlatpakTransaction> transaction_guard(transaction);

  gchar* tid = g_strdup(id.c_str());

  // Install only dependencies
  flatpak_transaction_set_no_interaction(transaction, TRUE);
  flatpak_transaction_set_disable_dependencies(transaction, FALSE);
  flatpak_transaction_set_disable_related(transaction, TRUE);
  flatpak_transaction_set_disable_prune(transaction, TRUE);
  flatpak_transaction_set_reinstall(transaction, FALSE);
  flatpak_transaction_set_no_pull(transaction, FALSE);
  flatpak_transaction_set_no_deploy(transaction, FALSE);

  g_object_set_data_full(G_OBJECT(transaction), "transaction_id", tid, g_free);
  g_object_set_data(G_OBJECT(transaction), "installation", installation);
  g_signal_connect(transaction, "new-operation", G_CALLBACK(OnNewOperation),
                   this);
  g_signal_connect(transaction, "operation-done",
                   G_CALLBACK(OnOperationComplete), this);
  g_signal_connect(transaction, "operation-error", G_CALLBACK(OnOperationError),
                   this);
  g_signal_connect(transaction, "ready", G_CALLBACK(OnTransactionReady), this);

  gboolean install = flatpak_transaction_add_install(
      transaction, remote_name.c_str(), ref_name.c_str(), nullptr, &error);

  if (error || !install) {
    std::string error_msg = error ? error->message : "Failed to add install";
    spdlog::error("[FlatpakPlugin] Failed to add install operation: {}",
                  error_msg);
    if (error) {
      g_clear_error(&error);
    }
    asio::post(*strand_, [this, id, callback, error_msg]() {
      operation_tracker_->SendOperationFinish(id, "install", false);
      callback(ErrorOr<bool>(FlutterError("ADD_INSTALL_ERROR", error_msg)));
    });
    return;
  }

  spdlog::info("[FlatpakPlugin] Added install operation, starting transaction");

  // Notify Flutter that the installation is starting
  flutter::EncodableMap start_event;
  start_event[flutter::EncodableValue("type")] =
      flutter::EncodableValue("installation_started");
  start_event[flutter::EncodableValue("app_id")] = flutter::EncodableValue(id);
  start_event[flutter::EncodableValue("ref")] =
      flutter::EncodableValue(ref_name);
  SendTransactionEvent(id, start_event);

  // transfer ownership to thread
  auto transaction_raw = transaction_guard.release();
  auto installation_raw = installation_guard.release();
  auto found_ref_raw = found_ref_guard.release();

  auto self = shared_from_this();

  // run transaction in a detached thread
  std::thread([self, transaction_raw, callback, ref_name, remote_name,
               strand_ptr = strand_, installation_raw, found_ref_raw, id]() {
    pthread_setname_np(pthread_self(), "flatpak-install");

    GError* error = nullptr;

    spdlog::info(
        "[FlatpakPlugin] ===== PHASE 1: Installing dependencies =====");
    gboolean success =
        flatpak_transaction_run(transaction_raw, nullptr, &error);
    spdlog::info("[FlatpakPlugin] Phase 1 transaction returned: {}",
                 success ? "TRUE" : "FALSE");

    std::string err_msg;
    if (error) {
      err_msg = error->message;
      spdlog::error("[FlatpakPlugin] Phase 1 error: {}", err_msg);
      g_clear_error(&error);
    }

    if (!success) {
      spdlog::error("[FlatpakPlugin] Phase 1 (dependencies) failed");
      asio::post(*strand_ptr, [self, callback, err_msg, transaction_raw,
                               found_ref_raw, installation_raw, id]() {
        self->operation_tracker_->SendOperationFinish(id, "install", false);

        spdlog::info(
            "[FlatpakPlugin] Cleaning up artifacts for failed install: {}", id);
        self->ApplicationStop(id);

        callback(ErrorOr<bool>(FlutterError(
            "INSTALL_FAILED", "Dependency installation failed: " + err_msg)));
        g_object_unref(transaction_raw);
        g_object_unref(installation_raw);
        g_object_unref(found_ref_raw);
      });
      return;
    }

    // check if the app was already installed
    spdlog::info(
        "[FlatpakPlugin] Phase 1 complete, checking if app was installed...");

    // wait for filesystem sync
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Check if the app is now installed, it might have been installed in phase
    // 1
    GError* check_error = nullptr;
    auto fresh_install = flatpak_installation_new_user(nullptr, &check_error);
    bool app_already_installed = false;

    if (!check_error && fresh_install) {
      const auto check_ref = flatpak_installation_get_installed_ref(
          fresh_install, FLATPAK_REF_KIND_APP,
          flatpak_ref_get_name(found_ref_raw),
          flatpak_ref_get_arch(found_ref_raw),
          flatpak_ref_get_branch(found_ref_raw), nullptr, &check_error);

      if (check_ref) {
        spdlog::info(
            "[FlatpakPlugin] App was installed in Phase 1! Skipping Phase 2.");
        g_object_unref(check_ref);
        app_already_installed = true;
      }
      if (check_error) {
        g_clear_error(&check_error);
      }
      g_object_unref(fresh_install);
    }

    // If the app is already installed, skip Phase 2
    if (app_already_installed) {
      spdlog::info("[FlatpakPlugin] Installation complete after Phase 1");
      asio::post(*strand_ptr, [self, callback, ref_name, transaction_raw,
                               found_ref_raw, installation_raw, id]() {
        self->operation_tracker_->SendOperationFinish(id, "install", true);
        flutter::EncodableMap complete_event;
        complete_event[flutter::EncodableValue("type")] =
            flutter::EncodableValue("install_complete");
        complete_event[flutter::EncodableValue("message")] =
            flutter::EncodableValue("Installation completed successfully");
        complete_event[flutter::EncodableValue("ref")] =
            flutter::EncodableValue(ref_name);
        self->SendTransactionEvent(id, complete_event);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        spdlog::info("[FlatpakPlugin] Application '{}' successfully installed",
                     ref_name);
        callback(ErrorOr<bool>(true));

        g_object_unref(transaction_raw);
        g_object_unref(installation_raw);
        g_object_unref(found_ref_raw);
      });
      return;
    }

    // create a new transaction for just the app, with dependencies disabled
    GError* phase2_error = nullptr;
    FlatpakTransaction* phase2_transaction =
        flatpak_transaction_new_for_installation(installation_raw, nullptr,
                                                 &phase2_error);

    if (phase2_error || !phase2_transaction) {
      std::string phase2_err = phase2_error
                                   ? phase2_error->message
                                   : "Failed to create phase 2 transaction";
      spdlog::error("[FlatpakPlugin] Phase 2 transaction creation failed: {}",
                    phase2_err);
      if (phase2_error)
        g_clear_error(&phase2_error);

      asio::post(*strand_ptr, [self, callback, phase2_err, transaction_raw,
                               found_ref_raw, installation_raw, id]() {
        spdlog::info(
            "[FlatpakPlugin] Cleaning up artifacts for failed install: {}", id);
        self->ApplicationStop(id);

        callback(ErrorOr<bool>(FlutterError("TRANSACTION_ERROR", phase2_err)));
        g_object_unref(transaction_raw);
        g_object_unref(installation_raw);
        g_object_unref(found_ref_raw);
      });
      return;
    }
    g_object_set_data_full(G_OBJECT(phase2_transaction), "transaction_id",
                           g_strdup(id.c_str()), g_free);
    flatpak_transaction_set_no_interaction(phase2_transaction, TRUE);
    flatpak_transaction_set_disable_dependencies(phase2_transaction, TRUE);
    flatpak_transaction_set_disable_related(phase2_transaction, TRUE);
    flatpak_transaction_set_disable_prune(phase2_transaction, TRUE);
    flatpak_transaction_set_reinstall(phase2_transaction, FALSE);
    flatpak_transaction_set_no_pull(phase2_transaction, FALSE);
    flatpak_transaction_set_no_deploy(phase2_transaction, FALSE);

    g_signal_connect(phase2_transaction, "new-operation",
                     G_CALLBACK(FlatpakShim::OnNewOperation), self.get());
    g_signal_connect(phase2_transaction, "operation-done",
                     G_CALLBACK(FlatpakShim::OnOperationComplete), self.get());
    g_signal_connect(phase2_transaction, "operation-error",
                     G_CALLBACK(FlatpakShim::OnOperationError), self.get());
    gboolean app_added = flatpak_transaction_add_install(
        phase2_transaction, remote_name.c_str(), ref_name.c_str(), nullptr,
        &phase2_error);

    if (phase2_error || !app_added) {
      std::string add_err =
          phase2_error ? phase2_error->message : "Failed to add app install";
      spdlog::error("[FlatpakPlugin] Failed to add app to phase 2: {}",
                    add_err);
      if (phase2_error)
        g_clear_error(&phase2_error);
      g_object_unref(phase2_transaction);

      asio::post(*strand_ptr, [self, callback, add_err, transaction_raw,
                               found_ref_raw, installation_raw, id]() {
        spdlog::info(
            "[FlatpakPlugin] Cleaning up artifacts for failed install: {}", id);
        self->ApplicationStop(id);

        callback(ErrorOr<bool>(FlutterError("ADD_INSTALL_ERROR", add_err)));
        g_object_unref(transaction_raw);
        g_object_unref(installation_raw);
        g_object_unref(found_ref_raw);
      });
      return;
    }
    gboolean phase2_success =
        flatpak_transaction_run(phase2_transaction, nullptr, &phase2_error);
    std::string phase2_err_msg;
    if (phase2_error) {
      phase2_err_msg = phase2_error->message;
      spdlog::error("[FlatpakPlugin] Phase 2 error: {}", phase2_err_msg);
      g_clear_error(&phase2_error);
    }

    g_object_unref(phase2_transaction);

    if (!phase2_success) {
      asio::post(*strand_ptr, [self, callback, phase2_err_msg, transaction_raw,
                               found_ref_raw, installation_raw, id]() {
        self->operation_tracker_->SendOperationFinish(id, "install", false);

        spdlog::info(
            "[FlatpakPlugin] Cleaning up artifacts for failed install: {}", id);
        self->ApplicationStop(id);

        callback(ErrorOr<bool>(FlutterError(
            "INSTALL_FAILED", "App installation failed: " + phase2_err_msg)));
        g_object_unref(transaction_raw);
        g_object_unref(installation_raw);
        g_object_unref(found_ref_raw);
      });
      return;
    }

    spdlog::info("[FlatpakPlugin] verifying installation...");
    std::this_thread::sleep_for(std::chrono::seconds(2));

    bool verified = false;
    GError* verify_error = nullptr;
    auto fresh_installation =
        flatpak_installation_new_user(nullptr, &verify_error);

    if (!verify_error && fresh_installation) {
      auto verify_ref = flatpak_installation_get_installed_ref(
          fresh_installation, FLATPAK_REF_KIND_APP,
          flatpak_ref_get_name(found_ref_raw),
          flatpak_ref_get_arch(found_ref_raw),
          flatpak_ref_get_branch(found_ref_raw), nullptr, &verify_error);

      if (verify_ref) {
        spdlog::info("[FlatpakPlugin] Installation verified: {}", ref_name);
        g_object_unref(verify_ref);
        verified = true;
      } else if (verify_error) {
        spdlog::error("[FlatpakPlugin] Verification failed: {}",
                      verify_error->message);
        g_clear_error(&verify_error);
      }

      g_object_unref(fresh_installation);
    }

    asio::post(*strand_ptr, [self, callback, verified, ref_name,
                             transaction_raw, found_ref_raw, installation_raw,
                             id]() {
      if (verified) {
        spdlog::info(
            "[FlatpakPlugin] Application '{}' successfully installed and "
            "verified",
            ref_name);
        self->operation_tracker_->SendOperationFinish(id, "install", true);
        flutter::EncodableMap complete_event;
        complete_event[flutter::EncodableValue("type")] =
            flutter::EncodableValue("install_complete");
        complete_event[flutter::EncodableValue("message")] =
            flutter::EncodableValue("Installation completed and verified");
        complete_event[flutter::EncodableValue("ref")] =
            flutter::EncodableValue(ref_name);
        self->SendTransactionEvent(id, complete_event);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        callback(ErrorOr<bool>(true));
      } else {
        spdlog::error(
            "[FlatpakPlugin] Installation completed but verification failed");
        self->operation_tracker_->SendOperationFinish(id, "install", false);
        flutter::EncodableMap failed_event;
        failed_event[flutter::EncodableValue("type")] =
            flutter::EncodableValue("install_failed");
        failed_event[flutter::EncodableValue("error")] =
            flutter::EncodableValue("Installation completed but app not found");
        self->SendTransactionEvent(id, failed_event);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        callback(ErrorOr<bool>(
            FlutterError("INSTALL_VERIFICATION_FAILED",
                         "Installation completed but app not found")));
      }

      self->RemoveTransactionEvent(id);
      g_object_unref(transaction_raw);
      g_object_unref(installation_raw);
      g_object_unref(found_ref_raw);
    });
  }).detach();
}

void FlatpakShim::ApplicationUninstall(
    const std::string& id,
    const std::function<void(ErrorOr<bool>)>& callback) {
  if (id.empty()) {
    asio::post(*strand_, [callback]() {
      callback(ErrorOr<bool>(
          FlutterError("INVALID_APP_ID", "Application ID is required")));
    });
    return;
  }

  spdlog::debug("[FlatpakPlugin] Uninstalling application: {}", id);

  GError* error = nullptr;
  auto installation = flatpak_installation_new_user(nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                  error->message);
    g_clear_error(&error);
    callback(ErrorOr<bool>(
        FlutterError("UNINSTALL_ERROR", "Failed to get user installation")));
    return;
  }

  auto refs =
      flatpak_installation_list_installed_refs(installation, nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Failed to get installed apps: {}",
                  error->message);
    g_clear_error(&error);
    g_object_unref(installation);
    callback(ErrorOr<bool>(
        FlutterError("UNINSTALL_ERROR", "Failed to get installed apps")));
    return;
  }

  bool app_found = false;
  std::string found_app_name;
  std::string found_arch;
  std::string found_branch;

  // Search for the application/runtime
  for (guint i = 0; i < refs->len && !app_found; i++) {
    const auto ref =
        static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

    if (const auto ref_name = flatpak_ref_get_name(FLATPAK_REF(ref))) {
      std::string ref_name_str(ref_name);
      const auto ref_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
      const auto ref_branch = flatpak_ref_get_branch(FLATPAK_REF(ref));
      std::string full_app_id;
      if (flatpak_ref_get_kind(FLATPAK_REF(ref)) == FLATPAK_REF_KIND_APP) {
        full_app_id = "app/";
      } else if (flatpak_ref_get_kind(FLATPAK_REF(ref)) ==
                 FLATPAK_REF_KIND_RUNTIME) {
        full_app_id = "runtime/";
      }

      full_app_id += ref_name_str + "/";
      full_app_id += (ref_arch ? ref_arch : "");
      full_app_id += "/";
      full_app_id += (ref_branch ? ref_branch : "");

      if (full_app_id == id || ref_name_str == id) {
        found_app_name = ref_name_str;
        found_arch = ref_arch ? ref_arch : "";
        found_branch = ref_branch ? ref_branch : "";
        app_found = true;
        g_object_ref(ref);
        break;
      }
    }
  }

  // some apps maybe installed system-wide, check there too
  if (!app_found) {
    g_ptr_array_unref(refs);
    g_object_unref(installation);
    installation = flatpak_installation_new_system(nullptr, &error);
    refs =
        flatpak_installation_list_installed_refs(installation, nullptr, &error);
    if (!error) {
      // Search for the application/runtime in system installation
      for (guint i = 0; i < refs->len && !app_found; i++) {
        const auto ref =
            static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

        if (const auto ref_name = flatpak_ref_get_name(FLATPAK_REF(ref))) {
          std::string ref_name_str(ref_name);
          const auto ref_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
          const auto ref_branch = flatpak_ref_get_branch(FLATPAK_REF(ref));
          std::string full_app_id;
          if (flatpak_ref_get_kind(FLATPAK_REF(ref)) == FLATPAK_REF_KIND_APP) {
            full_app_id = "app/";
          } else if (flatpak_ref_get_kind(FLATPAK_REF(ref)) ==
                     FLATPAK_REF_KIND_RUNTIME) {
            full_app_id = "runtime/";
          }

          full_app_id += ref_name_str + "/";
          full_app_id += (ref_arch ? ref_arch : "");
          full_app_id += "/";
          full_app_id += (ref_branch ? ref_branch : "");

          if (full_app_id == id || ref_name_str == id) {
            found_app_name = ref_name_str;
            found_arch = ref_arch ? ref_arch : "";
            found_branch = ref_branch ? ref_branch : "";
            app_found = true;
            g_object_ref(ref);
            break;
          }
        }
      }
    }
  }
  g_ptr_array_unref(refs);

  if (!app_found) {
    spdlog::error("[FlatpakPlugin] Application '{}' not found", id);
    g_object_unref(installation);
    callback(
        ErrorOr<bool>(FlutterError("APP_NOT_FOUND", "Application not found")));
    return;
  }

  GObjectGuard<FlatpakInstallation> installation_guard(installation);
  FlatpakTransaction* transaction =
      flatpak_transaction_new_for_installation(installation, nullptr, &error);
  if (error) {
    std::string err_msg = error->message;
    spdlog::error("[FlatpakPlugin] Error creating transaction: {}", err_msg);
    g_clear_error(&error);
    asio::post(*strand_, [callback]() {
      callback(ErrorOr<bool>(
          FlutterError("TRANSACTION_ERROR", "Error creating transaction")));
    });
    return;
  }

  GObjectGuard<FlatpakTransaction> transaction_guard(transaction);

  gchar* tid = g_strdup(id.c_str());

  flatpak_transaction_set_no_interaction(transaction, TRUE);
  flatpak_transaction_set_disable_dependencies(transaction, FALSE);
  flatpak_transaction_set_disable_related(transaction, FALSE);
  flatpak_transaction_set_disable_prune(transaction, FALSE);
  g_object_set_data_full(G_OBJECT(transaction), "transaction_id", tid, g_free);
  g_object_set_data(G_OBJECT(transaction), "installation", installation);

  g_signal_connect(transaction, "new-operation", G_CALLBACK(OnNewOperation),
                   this);
  g_signal_connect(transaction, "operation-done",
                   G_CALLBACK(OnOperationComplete), this);
  g_signal_connect(transaction, "operation-error", G_CALLBACK(OnOperationError),
                   this);

  const std::string ref =
      "app/" + found_app_name + "/" + found_arch + "/" + found_branch;
  auto found_ref = flatpak_ref_parse(ref.c_str(), &error);
  if (error || !found_ref) {
    std::string err_msg = error ? error->message : "Failed to parse ref";
    spdlog::error("[FlatpakPlugin] Failed to parse ref '{}': {}", ref, err_msg);
    if (error) {
      g_clear_error(&error);
    }
    asio::post(*strand_, [callback, err_msg]() {
      callback(ErrorOr<bool>(FlutterError("PARSE_ERROR", err_msg)));
    });
    return;
  }

  GObjectGuard<FlatpakRef> found_ref_guard(found_ref);
  spdlog::info(
      "[FlatpakPlugin] Found installed app '{}' -> name: {}, arch: {}, "
      "branch: {}",
      id, found_app_name, found_arch, found_branch);

  const gboolean uninstall =
      flatpak_transaction_add_uninstall(transaction, ref.c_str(), &error);

  if (error || !uninstall) {
    std::string err_msg = error ? error->message : "Failed to add uninstall";
    spdlog::error("[FlatpakPlugin] Failed to uninstall '{}': {}", id, err_msg);
    if (error) {
      g_clear_error(&error);
    }
    asio::post(*strand_, [callback]() {
      callback(ErrorOr<bool>(
          FlutterError("UNINSTALL_FAILED", "Failed to uninstall application")));
    });
    return;
  }

  spdlog::info(
      "[FlatpakPlugin] Added uninstall operation, starting transaction");

  // Notify Flutter that the uninstallation is starting
  flutter::EncodableMap start_event;
  start_event[flutter::EncodableValue("type")] =
      flutter::EncodableValue("uninstallation_started");
  start_event[flutter::EncodableValue("app_id")] = flutter::EncodableValue(id);
  start_event[flutter::EncodableValue("ref")] =
      flutter::EncodableValue(found_app_name);
  SendTransactionEvent(id, start_event);

  // transfer ownership to thread
  auto transaction_raw = transaction_guard.release();
  auto installation_raw = installation_guard.release();
  auto found_ref_raw = found_ref_guard.release();
  auto self = shared_from_this();

  std::thread([self, transaction_raw, installation_raw, found_app_name,
               strand_ptr = strand_, found_ref_raw, callback, id]() {
    pthread_setname_np(pthread_self(), "flatpak-uninstall");

    GError* error = nullptr;
    spdlog::info("[FlatpakPlugin] Starting uninstall transaction...");

    // Run transaction with the dedicated context
    gboolean success =
        flatpak_transaction_run(transaction_raw, nullptr, &error);

    std::string err_msg;
    if (error) {
      err_msg = error->message;
      spdlog::error("[FlatpakPlugin] Uninstall transaction failed: {}",
                    err_msg);
      g_clear_error(&error);
    }

    if (success) {
      spdlog::info("[FlatpakPlugin] Uninstall {} transaction completed",
                   found_app_name);
    } else {
      spdlog::error("[FlatpakPlugin] Uninstall {} transaction failed",
                    found_app_name);
    }

    // Cleanup GObjects
    g_object_unref(transaction_raw);
    g_object_unref(installation_raw);
    g_object_unref(found_ref_raw);

    // Post result back to original strand
    asio::post(*strand_ptr, [self, callback, success, err_msg, id]() {
      if (!success) {
        flutter::EncodableMap failed_event;
        failed_event[flutter::EncodableValue("type")] =
            flutter::EncodableValue("uninstall_failed");
        failed_event[flutter::EncodableValue("error")] =
            flutter::EncodableValue("Uninstall failed: " + err_msg);
        self->SendTransactionEvent(id, failed_event);
        self->RemoveTransactionEvent(id);
        callback(ErrorOr<bool>(
            FlutterError("UNINSTALL_FAILED", "Uninstall failed: " + err_msg)));
        return;
      }

      self->permissions_portal_->RemoveAllAppPermissions(id, [self, callback,
                                                              id](bool ok) {
        if (!ok) {
          // Uninstall succeeded but permissions cleanup failed
          spdlog::error(
              "[FlatpakPlugin] Permissions cleanup failed for {}, "
              "app was uninstalled successfully",
              id);
        }

        flutter::EncodableMap complete_event;
        complete_event[flutter::EncodableValue("type")] =
            flutter::EncodableValue("uninstall_complete");
        complete_event[flutter::EncodableValue("message")] =
            flutter::EncodableValue("Uninstallation completed successfully");
        self->SendTransactionEvent(id, complete_event);
        self->RemoveTransactionEvent(id);
        callback(ErrorOr<bool>(true));
      });
    });
  }).detach();
}

void FlatpakShim::ApplicationUpdate(
    const std::string& id,
    const std::function<void(ErrorOr<bool>)>& callback) {
  if (id.empty()) {
    asio::post(*strand_, [callback]() {
      callback(ErrorOr<bool>(
          FlutterError("INVALID_APP_ID", "Application ID is required")));
    });
    return;
  }

  spdlog::debug("[FlatpakPlugin] Updating application: {}", id);
  // Check if the application is installed first
  FlatpakInstalledRef* installed = nullptr;
  std::string full_ref;
  GError* error = nullptr;
  auto installation = flatpak_installation_new_user(nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                  error->message);
    g_clear_error(&error);
    callback(ErrorOr<bool>(
        FlutterError("UPDATE_ERROR", "Failed to get user installation")));
  }

  auto refs =
      flatpak_installation_list_installed_refs(installation, nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Failed to get installed apps: {}",
                  error->message);
    g_clear_error(&error);
    g_object_unref(installation);
    callback(ErrorOr<bool>(
        FlutterError("UPDATE_ERROR", "Failed to get installed apps")));
  }

  bool app_found = false;
  std::string found_app_name;
  std::string found_arch;
  std::string found_branch;

  // Search for the application/runtime
  for (guint i = 0; i < refs->len && !app_found; i++) {
    const auto ref =
        static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

    if (const auto ref_name = flatpak_ref_get_name(FLATPAK_REF(ref))) {
      std::string ref_name_str(ref_name);
      const auto ref_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
      const auto ref_branch = flatpak_ref_get_branch(FLATPAK_REF(ref));
      std::string full_app_id;
      if (flatpak_ref_get_kind(FLATPAK_REF(ref)) == FLATPAK_REF_KIND_APP) {
        full_app_id = "app/";
      } else if (flatpak_ref_get_kind(FLATPAK_REF(ref)) ==
                 FLATPAK_REF_KIND_RUNTIME) {
        full_app_id = "runtime/";
      }

      full_app_id += ref_name_str + "/";
      full_app_id += (ref_arch ? ref_arch : "");
      full_app_id += "/";
      full_app_id += (ref_branch ? ref_branch : "");

      if (full_app_id == id || ref_name_str == id) {
        found_app_name = ref_name_str;
        found_arch = ref_arch ? ref_arch : "";
        found_branch = ref_branch ? ref_branch : "";
        app_found = true;
        full_ref = full_app_id;
        g_object_ref(ref);
        installed = ref;
        break;
      }
    }
  }

  // some apps maybe installed system-wide, check there too
  if (!app_found) {
    g_ptr_array_unref(refs);
    g_object_unref(installation);
    installation = flatpak_installation_new_system(nullptr, &error);
    refs =
        flatpak_installation_list_installed_refs(installation, nullptr, &error);
    if (!error) {
      // Search for the application/runtime in system installation
      for (guint i = 0; i < refs->len && !app_found; i++) {
        const auto ref =
            static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

        if (const auto ref_name = flatpak_ref_get_name(FLATPAK_REF(ref))) {
          std::string ref_name_str(ref_name);
          const auto ref_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
          const auto ref_branch = flatpak_ref_get_branch(FLATPAK_REF(ref));
          std::string full_app_id;
          if (flatpak_ref_get_kind(FLATPAK_REF(ref)) == FLATPAK_REF_KIND_APP) {
            full_app_id = "app/";
          } else if (flatpak_ref_get_kind(FLATPAK_REF(ref)) ==
                     FLATPAK_REF_KIND_RUNTIME) {
            full_app_id = "runtime/";
          }

          full_app_id += ref_name_str + "/";
          full_app_id += (ref_arch ? ref_arch : "");
          full_app_id += "/";
          full_app_id += (ref_branch ? ref_branch : "");

          if (full_app_id == id || ref_name_str == id) {
            found_app_name = ref_name_str;
            found_arch = ref_arch ? ref_arch : "";
            found_branch = ref_branch ? ref_branch : "";
            app_found = true;
            full_ref = full_app_id;
            g_object_ref(ref);
            installed = ref;
            break;
          }
        }
      }
    }
  }
  g_ptr_array_unref(refs);

  if (!app_found) {
    spdlog::error("[FlatpakPlugin] Application '{}' not found", id);
    g_object_unref(installation);
    callback(
        ErrorOr<bool>(FlutterError("APP_NOT_FOUND", "Application not found")));
    return;
  }

  GObjectGuard<FlatpakInstallation> installation_guard(installation);
  FlatpakTransaction* transaction =
      flatpak_transaction_new_for_installation(installation, nullptr, &error);
  if (error) {
    std::string err_msg = error->message;
    spdlog::error("[FlatpakPlugin] Error creating transaction: {}", err_msg);
    g_clear_error(&error);
    asio::post(*strand_, [callback]() {
      callback(ErrorOr<bool>(
          FlutterError("TRANSACTION_ERROR", "Error creating transaction")));
    });
    return;
  }

  GObjectGuard<FlatpakTransaction> transaction_guard(transaction);

  gchar* tid = g_strdup(id.c_str());

  flatpak_transaction_set_no_interaction(transaction, TRUE);
  flatpak_transaction_set_disable_dependencies(transaction, FALSE);
  flatpak_transaction_set_disable_related(transaction, FALSE);
  flatpak_transaction_set_disable_prune(transaction, FALSE);

  g_object_set_data_full(G_OBJECT(transaction), "transaction_id", tid, g_free);
  g_signal_connect(transaction, "new-operation", G_CALLBACK(OnNewOperation),
                   this);
  g_signal_connect(transaction, "operation-done",
                   G_CALLBACK(OnOperationComplete), this);
  g_signal_connect(transaction, "operation-error", G_CALLBACK(OnOperationError),
                   this);

  GObjectGuard<FlatpakRef> found_ref_guard(FLATPAK_REF(installed));
  spdlog::info(
      "[FlatpakPlugin] Found installed app '{}' -> name: {}, arch: {}, "
      "branch: {}",
      id, found_app_name, found_arch, found_branch);

  const gboolean update = flatpak_transaction_add_update(
      transaction, full_ref.c_str(), nullptr, nullptr, &error);

  if (error || !update) {
    std::string err_msg = error ? error->message : "Failed to add update";
    spdlog::error("[FlatpakPlugin] Failed to update '{}': {}", id, err_msg);
    if (error) {
      g_clear_error(&error);
    }
    asio::post(*strand_, [callback]() {
      callback(ErrorOr<bool>(
          FlutterError("UPDATE_FAILED", "Failed to update application")));
    });
    return;
  }

  spdlog::info("[FlatpakPlugin] Added update operation, starting transaction");

  // Notify Flutter that the update is starting
  flutter::EncodableMap start_event;
  start_event[flutter::EncodableValue("type")] =
      flutter::EncodableValue("update_started");
  start_event[flutter::EncodableValue("app_id")] = flutter::EncodableValue(id);
  start_event[flutter::EncodableValue("ref")] =
      flutter::EncodableValue(found_app_name);
  SendTransactionEvent(id, start_event);

  // transfer ownership to thread
  auto transaction_raw = transaction_guard.release();
  auto installation_raw = installation_guard.release();
  auto found_ref_raw = found_ref_guard.release();
  auto self = shared_from_this();

  std::thread([self, transaction_raw, installation_raw, found_app_name,
               strand_ptr = strand_, found_ref_raw, callback, id]() {
    pthread_setname_np(pthread_self(), "flatpak-update");

    GError* error = nullptr;
    spdlog::info("[FlatpakPlugin] Starting update transaction...");

    // Run transaction with the dedicated context
    gboolean success =
        flatpak_transaction_run(transaction_raw, nullptr, &error);

    std::string err_msg;
    if (error) {
      err_msg = error->message;
      spdlog::error("[FlatpakPlugin] update transaction failed: {}", err_msg);
      g_clear_error(&error);
    }

    if (success) {
      spdlog::info("[FlatpakPlugin] update {} transaction completed",
                   found_app_name);
    } else {
      spdlog::error("[FlatpakPlugin] update {} transaction failed",
                    found_app_name);
    }

    // Cleanup GObjects
    g_object_unref(transaction_raw);
    g_object_unref(installation_raw);
    g_object_unref(found_ref_raw);

    // Post result back to original strand
    asio::post(*strand_ptr, [self, callback, success, err_msg, id]() {
      if (success) {
        flutter::EncodableMap complete_event;
        complete_event[flutter::EncodableValue("type")] =
            flutter::EncodableValue("update_complete");
        complete_event[flutter::EncodableValue("message")] =
            flutter::EncodableValue("Update completed successfully");
        self->SendTransactionEvent(id, complete_event);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        callback(ErrorOr<bool>(true));
      } else {
        flutter::EncodableMap failed_event;
        failed_event[flutter::EncodableValue("type")] =
            flutter::EncodableValue("update_failed");
        failed_event[flutter::EncodableValue("error")] =
            flutter::EncodableValue("Update failed: " + err_msg);
        self->SendTransactionEvent(id, failed_event);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        callback(ErrorOr<bool>(
            FlutterError("UPDATE_FAILED", "update failed: " + err_msg)));
      }
      self->RemoveTransactionEvent(id);
    });
  }).detach();
}

void FlatpakShim::ApplicationStart(
    const std::string& id,
    asio::io_context::strand& strand,
    const std::shared_ptr<PortalManager>& portal_manager,
    const std::function<void(const ErrorOr<bool>&)>& completion_callback) {
  if (id.empty()) {
    completion_callback(ErrorOr<bool>(
        FlutterError("INVALID_APP_ID", "Application ID is required")));
    return;
  }

  spdlog::debug("[FlatpakPlugin] Starting application: {}", id);
  auto weak_self = weak_from_this();
  auto self = weak_self.lock();
  if (!self) {
    completion_callback(
        ErrorOr<bool>(FlutterError("CANCELLED", "Operation cancelled")));
    return;
  }

  // Check if the application is installed first
  FlatpakInstalledRef* installed = nullptr;
  GError* error = nullptr;
  auto installation = flatpak_installation_new_user(nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Failed to get user installation: {}",
                  error->message);
    g_clear_error(&error);
    completion_callback(ErrorOr<bool>(
        FlutterError("INSTALLATION_ERROR", "Failed to get user installation")));
    return;
  }

  auto refs =
      flatpak_installation_list_installed_refs(installation, nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Failed to get installed apps: {}",
                  error->message);
    g_clear_error(&error);
    g_object_unref(installation);
    completion_callback(ErrorOr<bool>(
        FlutterError("START_FAILED", "Failed to get installed apps")));
    return;
  }

  bool app_found = false;
  std::string found_app_name;
  std::string found_arch;
  std::string found_branch;

  // Search for the application
  for (guint i = 0; i < refs->len && !app_found; i++) {
    const auto ref =
        static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

    if (flatpak_ref_get_kind(FLATPAK_REF(ref)) != FLATPAK_REF_KIND_APP) {
      continue;
    }

    if (const auto ref_name = flatpak_ref_get_name(FLATPAK_REF(ref))) {
      std::string ref_name_str(ref_name);
      const auto ref_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
      const auto ref_branch = flatpak_ref_get_branch(FLATPAK_REF(ref));

      std::string full_app_id = "app/";
      full_app_id += ref_name_str + "/";
      full_app_id += (ref_arch ? ref_arch : "");
      full_app_id += "/";
      full_app_id += (ref_branch ? ref_branch : "");

      if (full_app_id == id || ref_name_str == id) {
        found_app_name = ref_name_str;
        found_arch = ref_arch ? ref_arch : "";
        found_branch = ref_branch ? ref_branch : "";
        app_found = true;
        g_object_ref(ref);
        installed = ref;
        break;
      }
    }
  }

  // some apps maybe installed system-wide, check there too
  if (!app_found) {
    g_ptr_array_unref(refs);
    g_object_unref(installation);
    installation = flatpak_installation_new_system(nullptr, &error);
    refs =
        flatpak_installation_list_installed_refs(installation, nullptr, &error);
    if (!error) {
      // Search for the application in system installation
      for (guint i = 0; i < refs->len && !app_found; i++) {
        const auto ref =
            static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

        if (flatpak_ref_get_kind(FLATPAK_REF(ref)) != FLATPAK_REF_KIND_APP) {
          continue;
        }

        if (const auto ref_name = flatpak_ref_get_name(FLATPAK_REF(ref))) {
          std::string ref_name_str(ref_name);
          const auto ref_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
          const auto ref_branch = flatpak_ref_get_branch(FLATPAK_REF(ref));
          std::string full_app_id = "app/";
          full_app_id += ref_name_str + "/";
          full_app_id += (ref_arch ? ref_arch : "");
          full_app_id += "/";
          full_app_id += (ref_branch ? ref_branch : "");

          if (full_app_id == id || ref_name_str == id) {
            found_app_name = ref_name_str;
            found_arch = ref_arch ? ref_arch : "";
            found_branch = ref_branch ? ref_branch : "";
            app_found = true;
            g_object_ref(ref);
            installed = ref;
            break;
          }
        }
      }
    }
  }
  g_ptr_array_unref(refs);

  if (!app_found) {
    spdlog::error("[FlatpakPlugin] Application '{}' not found", id);
    g_object_unref(installation);
    completion_callback(
        ErrorOr<bool>(FlutterError("APP_NOT_FOUND", "Application not found")));
    return;
  }

  spdlog::info(
      "[FlatpakPlugin] Would start application: {} (name: {}, arch: {}, "
      "branch: {})",
      id, found_app_name, found_arch, found_branch);

  g_object_ref(installed);
  g_object_ref(installation);

  struct LaunchState {
    FlatpakInstalledRef* installed{};
    FlatpakInstallation* installation{};
    std::string app_name;
    std::string arch;
    std::string branch;
    std::shared_ptr<PortalManager> portal_manager;
    std::function<void(const ErrorOr<bool>&)> callback;
    asio::io_context::strand* strand{};

    ~LaunchState() {
      if (installed)
        g_object_unref(installed);
      if (installation)
        g_object_unref(installation);
    }
  };

  auto state = std::make_shared<LaunchState>();
  state->installed = installed;
  state->installation = installation;
  state->app_name = found_app_name;
  state->arch = found_arch;
  state->branch = found_branch;
  state->portal_manager = portal_manager;
  state->callback = completion_callback;
  state->strand = &strand;

  self->RequestAppLaunchPermission(
      found_app_name, installed,
      [weak_self, state](const std::map<std::string, bool>& permissions) {
        auto self = weak_self.lock();
        if (!self) {
          state->callback(
              ErrorOr<bool>(FlutterError("CANCELLED", "Operation cancelled")));
          return;
        }
        spdlog::info("[FlatpakPlugin] Permissions granted: {}",
                     permissions.size());
        self->check_runtime(
            state->installed, state->installation, *(state->strand),
            [weak_self, state](const ErrorOr<bool>& runtime_result) {
              auto self = weak_self.lock();
              if (!self) {
                state->callback(ErrorOr<bool>(
                    FlutterError("CANCELLED", "Operation cancelled")));
                return;
              }

              if (runtime_result.has_error()) {
                state->callback(runtime_result);
                return;
              }

              if (!runtime_result.value()) {
                state->callback(ErrorOr<bool>(
                    FlutterError("RUNTIME_ERROR", "Runtime not available")));
                return;
              }

              // Create sandbox
              flatpak_plugin::FlatpakShim::create_sandbox(
                  state->installed, *(state->strand),
                  [weak_self, state](bool sandbox_success) {
                    auto self = weak_self.lock();
                    if (!self) {
                      state->callback(ErrorOr<bool>(
                          FlutterError("CANCELLED", "Operation cancelled")));
                      return;
                    }

                    if (!sandbox_success) {
                      state->callback(ErrorOr<bool>(FlutterError(
                          "START_FAILED", "Failed to create sandbox")));
                      return;
                    }

                    // Launch application
                    GError* error = nullptr;
                    FlatpakInstance* instance = nullptr;
                    flatpak_installation_launch_full(
                        state->installation, FLATPAK_LAUNCH_FLAGS_DO_NOT_REAP,
                        state->app_name.c_str(), state->arch.c_str(),
                        state->branch.c_str(), nullptr, &instance, nullptr,
                        &error);

                    if (error) {
                      spdlog::error("[FlatpakShim] Failed to launch: {}",
                                    error->message);
                      g_clear_error(&error);
                      state->callback(ErrorOr<bool>(
                          FlutterError("START_FAILED", "Failed to launch")));
                      return;
                    }

                    if (instance) {
                      pid_t pid = flatpak_instance_get_pid(instance);
                      spdlog::info("[FlatpakShim] Launched: {} (PID: {})",
                                   state->app_name, pid);

                      auto app_session = std::make_shared<MonitorSession>(
                          state->strand, state->app_name, pid,
                          state->portal_manager, instance);
                      {
                        std::lock_guard<std::mutex> lock(
                            FlatpakShim::monitor_mutex_);
                        flatpak_plugin::FlatpakShim::active_sessions_
                            [state->app_name] = app_session;
                      }
                      flatpak_plugin::FlatpakShim::monitor_app(app_session);
                      state->callback(ErrorOr<bool>(true));
                      return;
                    }

                    state->callback(ErrorOr<bool>(FlutterError(
                        "START_FAILED", "Failed to get instance")));
                  },
                  state->portal_manager.get());
            });
      });
}

ErrorOr<bool> FlatpakShim::ApplicationStop(const std::string& id) {
  if (id.empty()) {
    return ErrorOr<bool>(
        FlutterError("INVALID_APP_ID", "Application ID is required"));
  }

  spdlog::info("[FlatpakPlugin] Stopping application: {}", id);
  const auto app = active_sessions_.find(id);
  if (app == active_sessions_.end()) {
    return ErrorOr<bool>(false);
  }

  FlatpakInstance* instance = app->second->instance;

  const pid_t child_pid = flatpak_instance_get_child_pid(instance);
  if (child_pid > 0) {
    spdlog::info("[FlatpakPlugin] Sending SIGTERM to child PID: {}", child_pid);
    if (kill(child_pid, SIGTERM) != 0) {
      spdlog::error("[FlatpakPlugin] Failed to send SIGTERM: {}",
                    strerror(errno));
    }

    bool exited = false;
    for (int i = 0; i < 50; i++) {
      if (kill(child_pid, 0) != 0) {
        exited = true;
        spdlog::info("[FlatpakPlugin] App exited gracefully: {}", id);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!exited) {
      spdlog::error("[FlatpakPlugin] Forcing kill of entire process group");
      kill(child_pid, SIGKILL);
    }
  }

  active_sessions_.erase(app);
  spdlog::info("[FlatpakPlugin] App stopped and cleaned up: {}", id);
  return ErrorOr<bool>(true);
}

ErrorOr<flutter::EncodableList> FlatpakShim::get_remotes_by_installation_id(
    const std::string& installation_id) {
  try {
    FlatpakInstallation* installation = nullptr;
    GError* error = nullptr;

    if (installation_id == "user") {
      installation = flatpak_installation_new_user(nullptr, &error);
      if (error) {
        const std::string error_msg = error->message;
        g_clear_error(&error);
        return ErrorOr<flutter::EncodableList>(
            FlutterError("INSTALLATION_ERROR",
                         "Failed to get user installation: " + error_msg));
      }
    } else {
      // Check system installations
      const auto system_installations = get_system_installations();
      if (!system_installations) {
        return ErrorOr<flutter::EncodableList>(
            FlutterError("NO_INSTALLATIONS", "No system installations found"));
      }

      for (size_t i = 0; i < system_installations->len; i++) {
        auto* sys_installation = static_cast<FlatpakInstallation*>(
            g_ptr_array_index(system_installations, i));

        if (const auto id = flatpak_installation_get_id(sys_installation);
            id && installation_id == std::string(id)) {
          installation =
              static_cast<FlatpakInstallation*>(g_object_ref(sys_installation));
          break;
        }
      }

      g_ptr_array_unref(system_installations);

      if (!installation) {
        return ErrorOr<flutter::EncodableList>(FlutterError(
            "INSTALLATION_NOT_FOUND",
            "Installation with ID '" + installation_id + "' not found"));
      }
    }

    if (!installation) {
      return ErrorOr<flutter::EncodableList>(
          FlutterError("INSTALLATION_ERROR", "Failed to get installation"));
    }

    GPtrArray* remotes = get_remotes(installation);
    if (!remotes) {
      g_object_unref(installation);
      return ErrorOr(flutter::EncodableList{});
    }

    auto remote_list = convert_remotes_to_EncodableList(remotes);
    g_ptr_array_unref(remotes);
    g_object_unref(installation);

    spdlog::debug(
        "[FlatpakPlugin] Successfully retrieved {} remotes for installation {}",
        remote_list.size(), installation_id);
    return ErrorOr(std::move(remote_list));
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Exception getting remotes: {}", e.what());
    return ErrorOr<flutter::EncodableList>(FlutterError(
        "UNKNOWN_ERROR", "Failed to get remotes: " + std::string(e.what())));
  }
}

std::string FlatpakShim::find_remote_for_app(FlatpakInstallation* installation,
                                             const char* app_name,
                                             const char* app_arch,
                                             const char* app_branch) {
  GError* error = nullptr;
  auto remotes =
      flatpak_installation_list_remotes(installation, nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Error getting remotes: {}", error->message);
    g_clear_error(&error);
    return "";
  }

  for (guint i = 0; i < remotes->len; i++) {
    auto remote = static_cast<FlatpakRemote*>(g_ptr_array_index(remotes, i));
    const auto remote_name = flatpak_remote_get_name(remote);

    if (flatpak_remote_get_disabled(remote)) {
      continue;
    }

    auto remote_ref = flatpak_installation_fetch_remote_ref_sync(
        installation, remote_name, FLATPAK_REF_KIND_APP, app_name, app_arch,
        app_branch, nullptr, &error);

    if (remote_ref && !error) {
      g_object_unref(remote_ref);
      g_ptr_array_unref(remotes);
      return {remote_name};
    }

    if (error) {
      g_clear_error(&error);
    }
  }

  g_ptr_array_unref(remotes);
  return "";
}

std::pair<std::string, std::string> FlatpakShim::find_app_in_remotes(
    FlatpakInstallation* installation,
    const std::string& app_id) {
  std::vector<std::string> priority_remotes = {"flathub", "fedora",
                                               "gnome-nightly"};
  std::vector<std::string> common_branches = {"stable", "beta", "master"};
  std::string default_arch = flatpak_get_default_arch();

  // Try priority remotes first
  for (const auto& remote_name : priority_remotes) {
    for (const auto& branch_name : common_branches) {
      GError* error = nullptr;
      auto remote_ref = flatpak_installation_fetch_remote_ref_sync(
          installation, remote_name.c_str(), FLATPAK_REF_KIND_APP,
          app_id.c_str(), default_arch.c_str(), branch_name.c_str(), nullptr,
          &error);

      if (remote_ref && !error) {
        std::string ref_string = "app/";
        ref_string += app_id;
        ref_string += "/";
        ref_string += default_arch;
        ref_string += "/";
        ref_string += branch_name;

        spdlog::info("[FlatpakPlugin] Found '{}' in remote '{}' as '{}'",
                     app_id, remote_name, ref_string);
        g_object_unref(remote_ref);
        return {remote_name, ref_string};
      }

      if (error) {
        g_clear_error(&error);
      }
    }
  }

  return find_app_in_remotes_fallback(installation, app_id);
}

std::pair<std::string, std::string> FlatpakShim::find_app_in_remotes_fallback(
    FlatpakInstallation* installation,
    const std::string& app_id) {
  GError* error = nullptr;
  auto remotes =
      flatpak_installation_list_remotes(installation, nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Error getting remotes: {}", error->message);
    g_clear_error(&error);
    return {"", ""};
  }

  for (guint i = 0; i < remotes->len; i++) {
    auto remote = static_cast<FlatpakRemote*>(g_ptr_array_index(remotes, i));
    if (flatpak_remote_get_disabled(remote)) {
      continue;
    }

    const auto remote_name = flatpak_remote_get_name(remote);
    auto result = search_in_single_remote(installation, remote_name, app_id);
    if (!result.first.empty()) {
      g_ptr_array_unref(remotes);
      return result;
    }
  }

  g_ptr_array_unref(remotes);
  return {"", ""};
}

std::pair<std::string, std::string> FlatpakShim::search_in_single_remote(
    FlatpakInstallation* installation,
    const char* remote_name,
    const std::string& app_id) {
  GError* error = nullptr;
  auto remote_refs = flatpak_installation_list_remote_refs_sync(
      installation, remote_name, nullptr, &error);

  if (error) {
    spdlog::error("[FlatpakPlugin] Skipping remote '{}': {}", remote_name,
                  error->message);
    g_clear_error(&error);
    return {"", ""};
  }

  for (guint j = 0; j < remote_refs->len; j++) {
    auto remote_ref =
        static_cast<FlatpakRemoteRef*>(g_ptr_array_index(remote_refs, j));

    if (flatpak_ref_get_kind(FLATPAK_REF(remote_ref)) != FLATPAK_REF_KIND_APP) {
      continue;
    }

    const char* ref_name = flatpak_ref_get_name(FLATPAK_REF(remote_ref));
    if (ref_name && app_id == std::string(ref_name)) {
      std::string ref_string = std::string("app/") + ref_name + "/" +
                               flatpak_ref_get_arch(FLATPAK_REF(remote_ref)) +
                               "/" +
                               flatpak_ref_get_branch(FLATPAK_REF(remote_ref));

      spdlog::info("[FlatpakPlugin] Found '{}' in remote '{}' as '{}'", app_id,
                   remote_name, ref_string);
      g_ptr_array_unref(remote_refs);
      return {std::string(remote_name), ref_string};
    }
  }

  g_ptr_array_unref(remote_refs);
  return {"", ""};
}

flutter::EncodableList FlatpakShim::convert_remotes_to_EncodableList(
    const GPtrArray* remotes) {
  flutter::EncodableList result;

  if (!remotes) {
    spdlog::error("[FlatpakPlugin] Received null remotes array");
    return result;
  }

  for (size_t j = 0; j < remotes->len; j++) {
    const auto remote =
        static_cast<FlatpakRemote*>(g_ptr_array_index(remotes, j));
    if (!remote) {
      spdlog::error("[FlatpakPlugin] Null remote at index {}", j);
      continue;
    }

    try {
      const auto name = flatpak_remote_get_name(remote);
      const auto url = flatpak_remote_get_url(remote);

      // Skip remotes without required fields
      if (!name || !url) {
        continue;
      }

      const auto collection_id = flatpak_remote_get_collection_id(remote);
      const auto title = flatpak_remote_get_title(remote);
      const auto comment = flatpak_remote_get_comment(remote);
      const auto description = flatpak_remote_get_description(remote);
      const auto homepage = flatpak_remote_get_homepage(remote);
      const auto icon = flatpak_remote_get_icon(remote);
      const auto default_branch = flatpak_remote_get_default_branch(remote);
      const auto main_ref = flatpak_remote_get_main_ref(remote);
      const auto filter = flatpak_remote_get_filter(remote);
      const bool gpg_verify = flatpak_remote_get_gpg_verify(remote);
      const bool no_enumerate = flatpak_remote_get_noenumerate(remote);
      const bool no_deps = flatpak_remote_get_nodeps(remote);
      const bool disabled = flatpak_remote_get_disabled(remote);
      const int32_t prio = flatpak_remote_get_prio(remote);

      const auto default_arch = flatpak_get_default_arch();
      auto appstream_timestamp_path = g_file_get_path(
          flatpak_remote_get_appstream_timestamp(remote, default_arch));
      auto appstream_dir_path = g_file_get_path(
          flatpak_remote_get_appstream_dir(remote, default_arch));

      auto appstream_timestamp = get_appstream_timestamp(
          appstream_timestamp_path ? appstream_timestamp_path : "");
      char formatted_time[64] = {0};
      format_time_iso8601(appstream_timestamp, formatted_time,
                          sizeof(formatted_time));

      result.emplace_back(flutter::CustomEncodableValue(Remote(
          std::string(name), std::string(url),
          collection_id ? std::string(collection_id) : "",
          title ? std::string(title) : "", comment ? std::string(comment) : "",
          description ? std::string(description) : "",
          homepage ? std::string(homepage) : "", icon ? std::string(icon) : "",
          default_branch ? std::string(default_branch) : "",
          main_ref ? std::string(main_ref) : "",
          FlatpakRemoteTypeToString(flatpak_remote_get_remote_type(remote)),
          filter ? std::string(filter) : "", std::string(formatted_time),
          appstream_dir_path ? std::string(appstream_dir_path) : "", gpg_verify,
          no_enumerate, no_deps, disabled, static_cast<int64_t>(prio))));

      if (appstream_timestamp_path) {
        g_free(appstream_timestamp_path);
      }

      if (appstream_dir_path) {
        g_free(appstream_dir_path);
      }
    } catch (const std::exception& e) {
      spdlog::error("[FlatpakPlugin] Received exception {}", e.what());
    }
  }
  spdlog::debug("[FlatpakPlugin] Converted {} remotes to encodable list ",
                result.size());
  return result;
}

flutter::EncodableList FlatpakShim::convert_applications_to_EncodableList(
    const GPtrArray* applications,
    FlatpakRemote* remote) {
  auto result = flutter::EncodableList();
  if (!applications) {
    spdlog::error("[FlatpakPlugin] Converter received null apps array");
    return result;
  }

  const auto default_arch = flatpak_get_default_arch();
  auto appstream_dir = flatpak_remote_get_appstream_dir(remote, default_arch);
  std::optional<AppstreamCatalog> catalog;
  if (appstream_dir) {
    if (auto appstream_path = g_file_get_path(appstream_dir)) {
      const std::string appstream_file =
          std::string(appstream_path) + "/appstream.xml";
      catalog = AppstreamCatalog(appstream_file, "en");
      spdlog::debug("[FlatpakPlugin] AppstreamCatalog loaded {} components",
                    catalog->getTotalComponentCount());
      g_free(appstream_path);
    }
  }

  result.reserve(applications->len);
  for (size_t j = 0; j < applications->len; j++) {
    const auto app_ref =
        static_cast<FlatpakRemoteRef*>(g_ptr_array_index(applications, j));
    if (!app_ref) {
      spdlog::error("[FlatpakPlugin] null application at index {}", j);
      continue;
    }

    if (flatpak_ref_get_kind(FLATPAK_REF(app_ref)) != FLATPAK_REF_KIND_APP) {
      continue;
    }
    try {
      const auto name = flatpak_ref_get_name(FLATPAK_REF(app_ref));
      if (!name) {
        spdlog::error(
            "[FlatpakPlugin] Application at index {} has no name, skipping", j);
        continue;
      }

      auto app_component = create_component(app_ref, catalog);
      if (app_component.has_value()) {
        result.emplace_back(
            flutter::CustomEncodableValue(app_component.value()));
      }
    } catch (const std::exception& e) {
      spdlog::error("[FlatpakPlugin] Received exception {}", e.what());
    }
  }
  spdlog::debug("[FlatpakPlugin] Converted {} applications to encodable list",
                result.size());
  return result;
}

std::string FlatpakShim::get_metadata_as_string(
    FlatpakInstalledRef* installed_ref) {
  GError* error = nullptr;

  const auto g_bytes =
      flatpak_installed_ref_load_metadata(installed_ref, nullptr, &error);

  if (!g_bytes) {
    if (error != nullptr) {
      spdlog::error("[FlatpakPlugin] Error loading metadata: {}\n",
                    error->message);
      g_clear_error(&error);
    }
    return {};
  }

  gsize size;
  const auto data = static_cast<const char*>(g_bytes_get_data(g_bytes, &size));
  std::string result(data, size);
  g_bytes_unref(g_bytes);
  return result;
}

std::string FlatpakShim::get_appdata_as_string(
    FlatpakInstalledRef* installed_ref) {
  GError* error = nullptr;
  const auto g_bytes =
      flatpak_installed_ref_load_appdata(installed_ref, nullptr, &error);
  if (!g_bytes) {
    if (error != nullptr) {
      spdlog::error("[FlatpakPlugin] {}", error->message);
      g_clear_error(&error);
    }
    return {};
  }

  gsize size;
  const auto data =
      static_cast<const uint8_t*>(g_bytes_get_data(g_bytes, &size));
  const std::vector<char> compressedData(data, data + size);
  std::vector<char> decompressedData;
  decompress_gzip(compressedData, decompressedData);
  std::string decompressedString(decompressedData.begin(),
                                 decompressedData.end());
  return decompressedString;
}

std::vector<char> FlatpakShim::decompress_gzip(
    const std::vector<char>& compressedData,
    std::vector<char>& decompressedData) {
  z_stream zs = {};
  if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
    spdlog::error("[FlatpakPlugin] Unable to initialize zlib inflate");
    return {};
  }
  zs.next_in =
      reinterpret_cast<Bytef*>(const_cast<char*>(compressedData.data()));
  zs.avail_in = static_cast<uInt>(compressedData.size());
  int zlibResult;
  char buffer[BUFFER_SIZE];
  do {
    zs.next_out = reinterpret_cast<Bytef*>(buffer);
    zs.avail_out = BUFFER_SIZE;
    zlibResult = inflate(&zs, 0);
    decompressedData.insert(decompressedData.end(), buffer,
                            buffer + BUFFER_SIZE - zs.avail_out);
  } while (zlibResult == Z_OK);
  inflateEnd(&zs);
  if (zlibResult != Z_STREAM_END) {
    spdlog::error("[FlatpakPlugin] Gzip decompression error");
    return {};
  }
  return decompressedData;
}

std::optional<Application> FlatpakShim::create_component(
    FlatpakRemoteRef* app_ref,
    const std::optional<AppstreamCatalog>& app_catalog) {
  if (!app_ref) {
    spdlog::error("[FlatpakPlugin] Cannot create component from null");
    return std::nullopt;
  }

  try {
    const char* app_id = flatpak_ref_get_name(FLATPAK_REF(app_ref));
    const char* remote_name = flatpak_remote_ref_get_remote_name(app_ref);
    const char* eol = flatpak_remote_ref_get_eol(app_ref);
    const char* eol_rebase = flatpak_remote_ref_get_eol_rebase(app_ref);
    const guint64 installed_size =
        flatpak_remote_ref_get_installed_size(app_ref);

    std::string name = app_id;
    std::string summary;
    std::string version;
    std::string license;
    std::string content_rating_type;
    flutter::EncodableMap content_rating;
    std::string metadata;
    std::string appdata;

    // fill Application data fields from catalog
    if (app_catalog.has_value()) {
      auto component_optional = app_catalog->searchById(std::string(app_id));
      if (component_optional.has_value()) {
        const auto& component = component_optional.value();
        name = component.getName();
        summary = component.getSummary();
        if (component.getVersion().has_value()) {
          version = component.getVersion().value();
        }
        if (component.getProjectLicense().has_value()) {
          license = component.getProjectLicense().value();
        }
        if (component.getContentRatingType().has_value()) {
          content_rating_type = component.getContentRatingType().value();
        }
        if (component.getContentRating().has_value()) {
          const auto& rating_map = component.getContentRating().value();
          for (const auto& [key, val] : rating_map) {
            content_rating[flutter::EncodableValue(key)] =
                flutter::EncodableValue(Component::RatingValueToString(val));
          }
        }
        metadata = create_metadata(component);
        appdata = create_appdata(component);
      }
    }
    flutter::EncodableList subpaths;
    return Application(
        name, app_id ? std::string(app_id) : "", summary, version,
        remote_name ? std::string(remote_name) : "", license,
        static_cast<int64_t>(installed_size), "", false, content_rating_type,
        content_rating, "", eol ? std::string(eol) : "",
        eol_rebase ? std::string(eol_rebase) : "", subpaths, metadata, appdata);
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Component creation failed for : {}",
                  e.what());
    return std::nullopt;
  }
}

std::string FlatpakShim::create_metadata(const Component& component) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

  document.AddMember(
      "id", rapidjson::Value(component.getId().c_str(), allocator), allocator);
  document.AddMember(
      "pkgname", rapidjson::Value(component.getPkgName().c_str(), allocator),
      allocator);
  if (component.getVersion().has_value()) {
    document.AddMember(
        "version",
        rapidjson::Value(component.getVersion().value().c_str(), allocator),
        allocator);
  }
  if (component.getArchitecture().has_value()) {
    document.AddMember(
        "architecture",
        rapidjson::Value(component.getArchitecture().value().c_str(),
                         allocator),
        allocator);
  }
  if (component.getBundle().has_value()) {
    document.AddMember(
        "bundle",
        rapidjson::Value(component.getBundle().value().c_str(), allocator),
        allocator);
  }
  if (component.getSourcePkgname().has_value()) {
    document.AddMember(
        "sourcepkgname",
        rapidjson::Value(component.getSourcePkgname().value().c_str(),
                         allocator),
        allocator);
  }
  if (component.getUrl().has_value()) {
    document.AddMember(
        "url", rapidjson::Value(component.getUrl().value().c_str(), allocator),
        allocator);
  }
  if (component.getMediaBaseurl().has_value()) {
    document.AddMember(
        "mediabaseurl",
        rapidjson::Value(component.getMediaBaseurl().value().c_str(),
                         allocator),
        allocator);
  }
  if (component.getCategories().has_value()) {
    rapidjson::Value categoriesArray(rapidjson::kArrayType);
    for (const auto& c : component.getCategories().value()) {
      categoriesArray.PushBack(rapidjson::Value(c.c_str(), allocator),
                               allocator);
    }
    document.AddMember("categories", categoriesArray, allocator);
  }
  if (component.getKeywords().has_value()) {
    rapidjson::Value keywordsArray(rapidjson::kArrayType);
    for (const auto& k : component.getKeywords().value()) {
      keywordsArray.PushBack(rapidjson::Value(k.c_str(), allocator), allocator);
    }
    document.AddMember("keywords", keywordsArray, allocator);
  }
  if (component.getLanguages().has_value()) {
    rapidjson::Value languagesArray(rapidjson::kArrayType);
    for (const auto& l : component.getLanguages().value()) {
      languagesArray.PushBack(rapidjson::Value(l.c_str(), allocator),
                              allocator);
    }
    document.AddMember("languages", languagesArray, allocator);
  }
  if (component.getDeveloper().has_value()) {
    rapidjson::Value developerArray(rapidjson::kArrayType);
    for (const auto& d : component.getDeveloper().value()) {
      developerArray.PushBack(rapidjson::Value(d.c_str(), allocator),
                              allocator);
    }
    document.AddMember("developer", developerArray, allocator);
  }

  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  return buffer.GetString();
}

std::string FlatpakShim::create_appdata(const Component& component) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

  // description
  if (component.getDescription().has_value()) {
    document.AddMember(
        "description",
        rapidjson::Value(component.getDescription().value().c_str(), allocator),
        allocator);
  }

  // screenshots
  if (component.getScreenshots().has_value()) {
    rapidjson::Value screenshotsArray(rapidjson::kArrayType);
    for (const auto& s : component.getScreenshots().value()) {
      rapidjson::Value screenshot(rapidjson::kObjectType);
      if (s.getType().has_value()) {
        screenshot.AddMember(
            "type", rapidjson::Value(s.getType().value().c_str(), allocator),
            allocator);
      }

      if (!s.getCaptions().empty()) {
        rapidjson::Value caption(rapidjson::kArrayType);
        for (const auto& c : s.getCaptions()) {
          caption.PushBack(rapidjson::Value(c.c_str(), allocator), allocator);
        }
        screenshot.AddMember("captions", caption, allocator);
      }

      if (s.getImages().has_value()) {
        rapidjson::Value imagesArray(rapidjson::kArrayType);
        for (const auto& i : s.getImages().value()) {
          rapidjson::Value image(rapidjson::kObjectType);
          if (i.getType().has_value()) {
            image.AddMember(
                "type",
                rapidjson::Value(i.getType().value().c_str(), allocator),
                allocator);
          }
          if (i.getUrl().has_value()) {
            image.AddMember(
                "url", rapidjson::Value(i.getUrl().value().c_str(), allocator),
                allocator);
          }
          if (i.getWidth().has_value()) {
            image.AddMember(
                "width",
                rapidjson::Value(std::to_string(i.getWidth().value()).c_str(),
                                 allocator),
                allocator);
          }
          if (i.getHeight().has_value()) {
            image.AddMember(
                "height",
                rapidjson::Value(std::to_string(i.getHeight().value()).c_str(),
                                 allocator),
                allocator);
          }
          imagesArray.PushBack(image, allocator);
        }
        screenshot.AddMember("images", imagesArray, allocator);
      }

      if (s.getVideo().has_value()) {
        rapidjson::Value video(rapidjson::kObjectType);
        if (s.getVideo().value().getUrl().has_value()) {
          video.AddMember(
              "url",
              rapidjson::Value(s.getVideo().value().getUrl().value().c_str(),
                               allocator),
              allocator);
        }
        if (s.getVideo().value().getWidth().has_value()) {
          video.AddMember(
              "width",
              rapidjson::Value(
                  std::to_string(s.getVideo().value().getWidth().value()),
                  allocator),
              allocator);
        }
        if (s.getVideo().value().getHeight().has_value()) {
          video.AddMember(
              "height",
              rapidjson::Value(
                  std::to_string(s.getVideo().value().getHeight().value()),
                  allocator),
              allocator);
        }
        if (s.getVideo().value().getCodec().has_value()) {
          video.AddMember(
              "codec",
              rapidjson::Value(s.getVideo().value().getCodec().value(),
                               allocator),
              allocator);
        }
        if (s.getVideo().value().getContainer().has_value()) {
          video.AddMember(
              "container",
              rapidjson::Value(s.getVideo().value().getContainer().value(),
                               allocator),
              allocator);
        }
        screenshot.AddMember("video", video, allocator);
      }
      screenshotsArray.PushBack(screenshot, allocator);
    }
    document.AddMember("screenshots", screenshotsArray, allocator);
  }

  // icons
  if (component.getIcons().has_value()) {
    rapidjson::Value iconsArray(rapidjson::kArrayType);
    for (const auto& i : component.getIcons().value()) {
      rapidjson::Value icon(rapidjson::kObjectType);
      if (i.getType().has_value()) {
        icon.AddMember("type",
                       rapidjson::Value(i.getType().value().c_str(), allocator),
                       allocator);
      }
      if (i.getPath().has_value()) {
        icon.AddMember("path",
                       rapidjson::Value(i.getPath().value().c_str(), allocator),
                       allocator);
      }
      if (i.getScale().has_value()) {
        icon.AddMember(
            "scale",
            rapidjson::Value(std::to_string(i.getScale().value()), allocator),
            allocator);
      }
      if (i.getWidth().has_value()) {
        icon.AddMember(
            "width",
            rapidjson::Value(std::to_string(i.getWidth().value()), allocator),
            allocator);
      }
      if (i.getHeight().has_value()) {
        icon.AddMember(
            "height",
            rapidjson::Value(std::to_string(i.getHeight().value()), allocator),
            allocator);
      }
      iconsArray.PushBack(icon, allocator);
    }
    document.AddMember("icons", iconsArray, allocator);
  }
  // releases
  if (component.getReleases().has_value()) {
    rapidjson::Value releasesArray(rapidjson::kArrayType);
    for (const auto& r : component.getReleases().value()) {
      rapidjson::Value release(rapidjson::kObjectType);
      release.AddMember("version",
                        rapidjson::Value(r.getVersion().c_str(), allocator),
                        allocator);
      release.AddMember("timestamp",
                        rapidjson::Value(r.getTimestamp().c_str(), allocator),
                        allocator);
      if (r.getDescription().has_value()) {
        release.AddMember(
            "description",
            rapidjson::Value(r.getDescription().value().c_str(), allocator),
            allocator);
      }
      if (r.getSize().has_value()) {
        release.AddMember("size",
                          rapidjson::Value(r.getSize().value(), allocator),
                          allocator);
      }
      releasesArray.PushBack(release, allocator);
    }
    document.AddMember("releases", releasesArray, allocator);
  }

  // agreements
  if (component.getAgreement().has_value()) {
    document.AddMember(
        "agreement",
        rapidjson::Value(component.getAgreement().value().c_str(), allocator),
        allocator);
  }

  // projectgroup
  if (component.getProjectGroup().has_value()) {
    document.AddMember(
        "projectgroup",
        rapidjson::Value(component.getProjectGroup().value().c_str(),
                         allocator),
        allocator);
  }

  // suggests
  if (component.getSuggests().has_value()) {
    rapidjson::Value suggestsArray(rapidjson::kArrayType);
    for (const auto& s : component.getSuggests().value()) {
      suggestsArray.PushBack(rapidjson::Value(s.c_str(), allocator), allocator);
    }
    document.AddMember("suggests", suggestsArray, allocator);
  }
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);

  return buffer.GetString();
}

void FlatpakShim::create_sandbox(FlatpakInstalledRef* installed_ref,
                                 const asio::io_context::strand& strand,
                                 const std::function<void(bool)>& callback,
                                 PortalManager* portal_manager) {
  const std::string metadata = get_metadata_as_string(installed_ref);
  const FlatpakShim::sandbox sandbox = parse_metadata(metadata);
  spdlog::debug("[FlatpakPlugin] Create sandbox for: {}",
                sandbox.application.name);

  setup_portal_sessions(
      sandbox, strand, portal_manager,
      [sandbox, callback](const ErrorOr<bool>& success) {
        if (!success.value()) {
          spdlog::error(
              "[FlatpakPlugin] Failed to setup portal interfaces for {}",
              sandbox.application.name);
          callback(false);
          return;
        }
        spdlog::debug("[FlatpakPlugin] Sandbox setup completed for {}",
                      sandbox.application.name);
        callback(true);
      });
}

void FlatpakShim::setup_portal_sessions(
    const sandbox& configs,
    const asio::io_context::strand& strand,
    PortalManager* portal_manager,
    const std::function<void(ErrorOr<bool>)>& callback) {
  std::vector<PortalInterface> interfaces;

  // setup access portal to launch first
  PortalInterface access_portal_interface;
  access_portal_interface.bus_type = BUS_TYPE::SESSION;
  access_portal_interface.interface_name = "org.freedesktop.impl.portal.Access";
  access_portal_interface.object_path = "/org/freedesktop/portal/desktop";
  access_portal_interface.service_name = "org.freedesktop.portal.desktop";
  interfaces.push_back(access_portal_interface);

  for (const auto& interface : configs.session_bus) {
    if (interface == "org.freedesktop.impl.portal.Access") {
      continue;
    }

    PortalInterface portal;
    portal.interface_name = interface;
    portal.bus_type = BUS_TYPE::SESSION;
    portal.service_name = interface;

    portal.object_path = "/" + interface;
    std::replace(portal.object_path.begin(), portal.object_path.end(), '.',
                 '/');
    interfaces.push_back(portal);
  }

  for (const auto& interface : configs.system_bus) {
    PortalInterface portal;
    portal.interface_name = interface;
    portal.bus_type = BUS_TYPE::SYSTEM;
    portal.service_name = interface;

    portal.object_path = "/" + interface;
    std::replace(portal.object_path.begin(), portal.object_path.end(), '.',
                 '/');
    interfaces.push_back(portal);
  }
  if (interfaces.empty()) {
    asio::post(strand, [callback, name = configs.application.name]() {
      spdlog::error("[FlatpakPlugin] No portal interfaces to register for {}",
                    name);
      callback(ErrorOr<bool>(true));
    });
    return;
  }

  try {
    portal_manager->register_application(configs.application.name, interfaces);
    asio::post(
        strand, [callback, name = configs.application.name, interfaces]() {
          spdlog::info("[FlatpakPlugin] Registered {} D-Bus interfaces for {}",
                       interfaces.size(), name);

          callback(ErrorOr<bool>(true));
        });
  } catch (const std::exception& e) {
    std::string error_message = e.what();
    asio::post(strand, [callback, error_message]() {
      spdlog::error("[FlatpakPlugin] Failed to register interfaces: {}",
                    error_message);
      callback(ErrorOr<bool>(false));
    });
  }
  // TODO: check and request permissions
}

FlatpakShim::sandbox FlatpakShim::parse_metadata(const std::string& metadata) {
  FlatpakShim::sandbox sandbox;
  auto application = extract_metadataSections(metadata, "Application");
  auto context = extract_metadataSections(metadata, "Context");
  const auto session_bus =
      extract_metadataSections(metadata, "Session Bus Policy");
  const auto system_bus =
      extract_metadataSections(metadata, "System Bus Policy");
  auto extra_data = extract_metadataSections(metadata, "Extra Data");
  const auto build = extract_metadataSections(metadata, "Build");

  sandbox.environment = extract_metadataSections(metadata, "Environment");

  // parse application data
  if (application.count("name")) {
    sandbox.application.name = application["name"];
  }
  if (application.count("runtime")) {
    sandbox.application.runtime = application["runtime"];
  }
  if (application.count("sdk")) {
    sandbox.application.sdk = application["sdk"];
  }
  if (application.count("base")) {
    sandbox.application.base = application["base"];
  }
  if (application.count("command")) {
    sandbox.application.command = application["command"];
  }

  // parse context
  if (context.count("shared")) {
    sandbox.context.shared =
        plugin_common::StringTools::split(context["shared"], ";");
  }
  if (context.count("sockets")) {
    sandbox.context.sockets =
        plugin_common::StringTools::split(context["sockets"], ";");
  }
  if (context.count("devices")) {
    sandbox.context.devices =
        plugin_common::StringTools::split(context["devices"], ";");
  }
  if (context.count("filesystems")) {
    sandbox.context.filesystems =
        plugin_common::StringTools::split(context["filesystems"], ";");
  }

  // parse session bus
  for (const auto& [fst, snd] : session_bus) {
    sandbox.session_bus.emplace_back(fst);
  }

  // parse system bus
  for (const auto& [fst, snd] : system_bus) {
    sandbox.system_bus.emplace_back(fst);
  }

  // parse extra data
  if (extra_data.count("name")) {
    sandbox.extra.name = extra_data["name"];
  }
  if (extra_data.count("checksum")) {
    sandbox.extra.checksum = extra_data["checksum"];
  }
  if (extra_data.count("size")) {
    sandbox.extra.size = extra_data["size"];
  }
  if (extra_data.count("uri")) {
    sandbox.extra.uri = extra_data["uri"];
  }

  // parse build
  if (build.count("built-extensions")) {
    sandbox.built_extensions =
        plugin_common::StringTools::split(context["built-extensions"], ";");
  }
  return sandbox;
}

std::map<std::string, std::string> FlatpakShim::extract_metadataSections(
    const std::string& metadata,
    const std::string& section) {
  std::map<std::string, std::string> values;

  std::string escapedName = std::regex_replace(
      section, std::regex(R"([\.\[\](){}*+?^$|\\])"), R"(\$&)");
  std::string pattern = R"(\[)" + escapedName + R"(\]\s*\n((?:[^[].*\n?)*))";
  std::regex sectionReg(pattern);
  if (std::smatch match; std::regex_search(metadata, match, sectionReg)) {
    std::string content = match[1];
    std::regex kvPattern(R"(^([a-zA-Z][a-zA-Z0-9_.-]*)\s*=\s*([^\n]+))");

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
      plugin_common::StringTools::trimSpaces(line);

      if (!line.empty() && line[0] == '[') {
        break;
      }

      if (std::smatch match2; std::regex_match(line, match2, kvPattern)) {
        std::string val = match2[2];
        plugin_common::StringTools::trimSpaces(val);
        values[match2[1]] = val;
      }
    }
  }
  return values;
}

FlatpakInstance* FlatpakShim::find_running_instance(const std::string& app_id) {
  auto instances = flatpak_instance_get_all();
  if (!instances)
    return nullptr;

  for (guint i = 0; i < instances->len; ++i) {
    auto* instance =
        static_cast<FlatpakInstance*>(g_ptr_array_index(instances, i));
    const char* instance_app_id = flatpak_instance_get_app(instance);
    if (!instance_app_id)
      return nullptr;

    if (app_id == instance_app_id && flatpak_instance_is_running(instance)) {
      g_object_ref(instance);
      return instance;
    }
  }
  return nullptr;
}

void FlatpakShim::monitor_app(const std::shared_ptr<MonitorSession>& session) {
  if (!session || session->cancelled) {
    return;
  }

  FlatpakInstance* live = find_running_instance(session->name);
  if (!live) {
    spdlog::info("[FlatpakPlugin] App {} stopped", session->name);
    cleanup_app(session);
    return;
  }

  g_object_unref(live);
  schedule_next_check(session);
}

void FlatpakShim::cleanup_app(const std::shared_ptr<MonitorSession>& session) {
  if (!session) {
    return;
  }
  session->cancelled = true;

  bool expected = false;
  if (!session->cancelled.compare_exchange_strong(expected, true)) {
    return;
  }

  if (session->timer) {
    session->timer->cancel();
  }

  if (auto pm = session->portal_manager.lock()) {
    try {
      pm->unregister_application(session->name);
      spdlog::debug("[FlatpakPlugin] Unregister portal for App {}",
                    session->name);
    } catch (std::exception& e) {
      spdlog::error("[FlatpakPlugin] Unregister portal for App {} failed: {}",
                    session->name, e.what());
    }
  }
  {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    active_sessions_.erase(session->name);
  }
  spdlog::info("[FlatpakPlugin] Cleaning up App {} completed", session->name);
}

void FlatpakShim::schedule_next_check(
    const std::shared_ptr<MonitorSession>& session) {
  if (!session || session->cancelled) {
    return;
  }
  std::lock_guard<std::mutex> lock(monitor_mutex_);
  if (active_sessions_.find(session->name) == active_sessions_.end()) {
    spdlog::error("[FlatpakPlugin] Session not found for app: {}",
                  session->name);
    return;
  }

  if (!session->timer) {
    spdlog::error("[FlatpakPlugin] Timer is null for app: {}", session->name);
    cleanup_app(session);
    return;
  }

  if (!session->strand) {
    spdlog::error("[FlatpakPlugin] io_context is null for app: {}",
                  session->name);
    cleanup_app(session);
    return;
  }
  try {
    session->timer->expires_after(std::chrono::milliseconds(500));

    std::weak_ptr<MonitorSession> weak_session = session;
    session->timer->async_wait(asio::bind_executor(
        *session->strand, [weak_session](const asio::error_code& ec) {
          auto session = weak_session.lock();
          if (!session) {
            return;
          }
          if (ec) {
            if (ec == asio::error::operation_aborted) {
              spdlog::debug("[FlatpakPlugin] Timer cancelled for {}",
                            session->name);
            } else {
              spdlog::error("[FlatpakPlugin] Timer error for {}: {}",
                            session->name, ec.message());
            }
            return;
          }
          monitor_app(session);
        }));
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Application {} failed: {}", session->name,
                  e.what());
    cleanup_app(session);
  }
}

void FlatpakShim::stop_all_monitoring() {
  std::vector<std::shared_ptr<MonitorSession>> to_cleanup;
  {
    std::lock_guard<std::mutex> lock(monitor_mutex_);

    spdlog::info("[FlatpakPlugin] Stopping {} active monitors",
                 active_sessions_.size());
    to_cleanup.reserve(active_sessions_.size());
    for (auto& [_, session] : active_sessions_) {
      if (session)
        to_cleanup.push_back(session);
    }
  }

  for (auto& session : to_cleanup) {
    cleanup_app(session);
  }

  active_sessions_.clear();
}

bool FlatpakShim::is_app_running(const std::string& app_name) {
  std::lock_guard<std::mutex> lock(monitor_mutex_);
  auto it = active_sessions_.find(app_name);
  if (it == active_sessions_.end()) {
    return false;
  }

  FlatpakInstance* live = find_running_instance(app_name);
  if (!live) {
    spdlog::error("[FlatpakPlugin] is_app_running: stale session for {} — ",
                  app_name);
    if (it->second) {
      it->second->cancelled.store(true);
      if (it->second->timer)
        it->second->timer->cancel();
    }
    active_sessions_.erase(it);
    return false;
  }
  g_object_unref(live);
  return true;
}

void FlatpakShim::check_runtime(
    FlatpakInstalledRef* installed_ref,
    FlatpakInstallation* installation,
    asio::io_context::strand& strand,
    const std::function<void(ErrorOr<bool>)>& callback) {
  if (!installed_ref) {
    asio::dispatch(strand, [callback]() {
      callback(
          ErrorOr<bool>(FlutterError("INVALID_REF", "Installed ref is null")));
    });
    return;
  }

  GError* error = nullptr;

  const std::string metadata = get_metadata_as_string(installed_ref);
  const FlatpakShim::sandbox sandbox = parse_metadata(metadata);
  std::string runtime = "runtime/" + sandbox.application.runtime;
  std::vector<std::string> extensions{};

  // space for debugging in system extensions
  const char* ref_name = flatpak_ref_get_name(FLATPAK_REF(installed_ref));
  auto remote_ref = find_app_in_remotes(installation, ref_name);
  std::string remote = remote_ref.first;
  auto ref = remote_ref.second.c_str();

  auto related_extensions = flatpak_installation_list_remote_related_refs_sync(
      installation, remote.c_str(), ref, nullptr, &error);

  if (error) {
    spdlog::error("[FlatpakPlugin] Failed to list related refs: {}",
                  error->message);
    g_clear_error(&error);
  } else if (related_extensions) {
    for (guint i = 0; i < related_extensions->len; i++) {
      const auto ext = static_cast<FlatpakRelatedRef*>(
          g_ptr_array_index(related_extensions, i));
      const char* related_name = flatpak_ref_get_name(FLATPAK_REF(ext));
      const char* related_branch = flatpak_ref_get_branch(FLATPAK_REF(ext));
      const char* related_arch = flatpak_ref_get_arch(FLATPAK_REF(ext));
      std::string related = "runtime/" + std::string(related_name) + "/" +
                            std::string(related_arch) + "/" +
                            std::string(related_branch);
      if (flatpak_related_ref_should_download(ext)) {
        extensions.emplace_back(related);
      }
    }
    g_ptr_array_unref(related_extensions);
  }

  const std::string& extension_remote = remote;
  const std::string& runtime_remote = remote;
  if (runtime.empty()) {
    spdlog::error("[FlatpakPlugin] No runtime specified for {}",
                  sandbox.application.name);
    asio::dispatch(strand, [callback]() {
      callback(ErrorOr<bool>(
          FlutterError("NO_RUNTIME", "App has no runtime requirement")));
    });
    return;
  }

  spdlog::debug("[FlatpakPlugin] Checking runtime: {} for {}", runtime,
                sandbox.application.name);

  if (!is_runtime_installed_for_app(runtime, installation)) {
    spdlog::info("[FlatpakPlugin] Runtime {} not installed, installing...",
                 runtime);

    install_runtime(
        runtime, runtime_remote, strand, installation,
        [callback, strand = &strand, installation, extensions, extension_remote,
         self = shared_from_this()](const ErrorOr<bool>& runtime_result) {
          if (!runtime_result.value()) {
            spdlog::error("[FlatpakPlugin] Failed to install runtime");
            asio::post(*strand, [callback, runtime_result]() {
              callback(runtime_result);
            });
            return;
          }

          spdlog::info("[FlatpakPlugin] Runtime installed successfully");

          if (!extensions.empty()) {
            spdlog::debug(
                "[FlatpakPlugin] Checking Extensions after installing runtime");
            self->install_extensions(
                extensions, extension_remote, *strand, installation,
                [callback, installation,
                 strand](const ErrorOr<bool>& ext_result) {
                  asio::post(*strand, [callback, ext_result, installation]() {
                    g_object_unref(installation);
                    callback(ext_result);
                  });
                });
          } else {
            asio::post(*strand, [callback, installation]() {
              g_object_unref(installation);
              callback(ErrorOr<bool>(true));
            });
          }
        });
    return;
  }

  // Runtime installed, check extensions
  spdlog::debug(
      "[FlatpakPlugin] Runtime already installed, checking extensions");

  if (extensions.empty()) {
    spdlog::info("[FlatpakPlugin] No extensions needed for {}",
                 sandbox.application.name);

    asio::dispatch(strand, [callback, installation]() {
      spdlog::debug("[FlatpakPlugin] Executing callback (no extensions)");
      g_object_unref(installation);
      callback(ErrorOr<bool>(true));
    });
    return;
  }

  spdlog::debug("[FlatpakPlugin] checking {} extensions", extensions.size());
  install_extensions(extensions, extension_remote, strand, installation,
                     [callback, strand_ptr = &strand,
                      installation](const ErrorOr<bool>& result) {
                       asio::post(*strand_ptr,
                                  [callback, result, installation]() {
                                    g_object_unref(installation);
                                    callback(result);
                                  });
                     });
}

bool FlatpakShim::is_runtime_installed_for_app(
    const std::string& runtime,
    FlatpakInstallation* installation) {
  GError* error = nullptr;
  auto refs = flatpak_installation_list_installed_refs_by_kind(
      installation, FLATPAK_REF_KIND_RUNTIME, nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Failed to fetch runtimes : {}",
                  error->message);
    g_clear_error(&error);
    return false;
  }
  bool found = false;
  for (guint i = 0; i < refs->len; i++) {
    auto* ref = static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

    const char* ref_name = flatpak_ref_get_name(FLATPAK_REF(ref));
    const char* ref_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
    const char* ref_branch = flatpak_ref_get_branch(FLATPAK_REF(ref));

    std::string ref_runtime = "runtime/" + std::string(ref_name) + '/' +
                              std::string(ref_arch) + '/' +
                              std::string(ref_branch);

    if (ref_runtime == runtime) {
      found = true;
      spdlog::debug("[FlatpakPlugin] Found runtime : {}", ref_runtime);
    }
  }
  g_clear_error(&error);
  g_ptr_array_unref(refs);
  return found;
}

void FlatpakShim::install_runtime(
    const std::string& runtime,
    const std::string& remote,
    asio::io_context::strand& strand,
    FlatpakInstallation* installation,
    const std::function<void(ErrorOr<bool>)>& complete_callback) {
  if (runtime.empty()) {
    asio::post(strand, [complete_callback]() {
      complete_callback(
          ErrorOr<bool>(FlutterError("INVALID_RUNTIME", "Runtime is empty")));
    });
    return;
  }
  GError* error = nullptr;
  spdlog::info("[FlatpakPlugin] Installing runtime {}", runtime);
  FlatpakTransaction* transaction =
      flatpak_transaction_new_for_installation(installation, nullptr, &error);
  if (error) {
    spdlog::error(
        "[FlatpakPlugin] Error installing runtime creating transction : {}",
        error->message);
    g_clear_error(&error);
    asio::post(strand, [complete_callback]() {
      complete_callback(ErrorOr<bool>(
          FlutterError("INVALID_RUNTIME", "error creating transction")));
    });
    return;
  }

  auto self = shared_from_this();
  flatpak_transaction_set_no_interaction(transaction, TRUE);
  flatpak_transaction_set_reinstall(transaction, FALSE);

  // connect all transaction signals
  g_signal_connect(transaction, "new-operation", G_CALLBACK(OnNewOperation),
                   self.get());
  g_signal_connect(transaction, "operation-done",
                   G_CALLBACK(OnOperationComplete), self.get());
  g_signal_connect(transaction, "operation-error", G_CALLBACK(OnOperationError),
                   self.get());
  g_signal_connect(transaction, "ready", G_CALLBACK(OnTransactionReady),
                   self.get());

  auto install = flatpak_transaction_add_install(
      transaction, remote.c_str(), runtime.c_str(), nullptr, &error);
  if (error || !install) {
    spdlog::error("[FlatpakPlugin] Error installing runtime : {}",
                  error->message);
    g_clear_error(&error);
    asio::post(strand, [complete_callback]() {
      complete_callback(ErrorOr<bool>(
          FlutterError("INSTALL_RUNTIME", "Error while add install")));
    });
    g_object_unref(transaction);
    return;
  }

  // run transaction async, since it will block the thread until it finishes.
  std::thread([transaction, strand_ptr = &strand, complete_callback, self,
               runtime]() {
    pthread_setname_np(pthread_self(), "flatpak-runtime");

    spdlog::info("[FlatpakPlugin] Starting runtime installation for: {}",
                 runtime);

    GError* error = nullptr;
    auto success = flatpak_transaction_run(transaction, nullptr, &error);

    std::string error_message;
    if (error) {
      error_message = error->message;
      spdlog::error("[FlatpakPlugin] Runtime installation error: {}",
                    error_message);
      g_clear_error(&error);
    }

    asio::post(*strand_ptr, [complete_callback, success, error_message,
                             runtime]() {
      if (success) {
        spdlog::info("[FlatpakPlugin] Runtime '{}' installed successfully",
                     runtime);
        complete_callback(ErrorOr<bool>(true));
      } else {
        spdlog::error("[FlatpakPlugin] Runtime '{}' installation failed: {}",
                      runtime, error_message);
        complete_callback(
            ErrorOr<bool>(FlutterError("INSTALL_RUNTIME", error_message)));
      }
    });

    g_object_unref(transaction);
    spdlog::debug("[FlatpakPlugin] Runtime installation thread exiting");
  }).detach();
}

void FlatpakShim::install_extensions(
    const std::vector<std::string>& extensions,
    const std::string& remote,
    asio::io_context::strand& strand,
    FlatpakInstallation* installation,
    const std::function<void(ErrorOr<bool>)>& complete_callback) {
  if (extensions.empty()) {
    spdlog::debug("[FlatpakPlugin] Installing extensions is empty");
    asio::dispatch(strand, [complete_callback]() {
      spdlog::debug("[FlatpakPlugin] Completing with success (no extensions)");
      complete_callback(ErrorOr<bool>(true));
    });
    return;
  }

  GError* error = nullptr;
  std::vector<std::string> missing_extensions;
  spdlog::info("[FlatpakPlugin] Installing {} extensions", extensions.size());

  for (const auto& extension : extensions) {
    FlatpakRef* ref = flatpak_ref_parse(extension.c_str(), &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Error parsing extension : {}",
                    error->message);
      g_clear_error(&error);
      continue;
    }
    FlatpakInstalledRef* is_installed = flatpak_installation_get_installed_ref(
        installation, FLATPAK_REF_KIND_RUNTIME, flatpak_ref_get_name(ref),
        flatpak_ref_get_arch(ref), flatpak_ref_get_branch(ref), nullptr,
        &error);

    if (is_installed) {
      spdlog::debug("[FlatpakPlugin] Extension {} already installed",
                    extension);
      g_object_unref(is_installed);
      g_object_unref(ref);
      continue;
    }
    if (error) {
      if (error->code == FLATPAK_ERROR_NOT_INSTALLED) {
        missing_extensions.emplace_back(extension);
        spdlog::debug("[FlatpakPlugin] Extension {} not installed", extension);
      } else {
        spdlog::error("[FlatpakPlugin] Error checking extension {}: {}",
                      extension, error->message);
      }
      g_clear_error(&error);
    } else {
      missing_extensions.emplace_back(extension);
      spdlog::debug("[FlatpakPlugin] Extension {} not installed", extension);
    }
    g_object_unref(ref);
  }

  if (missing_extensions.empty()) {
    spdlog::info("[FlatpakPlugin] All {} extensions already installed",
                 extensions.size());
    asio::dispatch(strand, [complete_callback]() {
      spdlog::debug("[FlatpakPlugin] Completing with success (all installed)");
      complete_callback(ErrorOr<bool>(true));
    });
    return;
  }
  FlatpakTransaction* transaction =
      flatpak_transaction_new_for_installation(installation, nullptr, &error);

  if (error) {
    std::string error_msg = error->message;
    spdlog::error("[FlatpakPlugin] Failed to create transaction: {}",
                  error_msg);
    g_clear_error(&error);
    asio::post(strand, [complete_callback, error_msg]() {
      complete_callback(
          ErrorOr<bool>(FlutterError("TRANSACTION_ERROR", error_msg)));
    });
    return;
  }

  auto self = shared_from_this();
  flatpak_transaction_set_no_interaction(transaction, TRUE);
  flatpak_transaction_set_reinstall(transaction, FALSE);
  flatpak_transaction_set_disable_dependencies(transaction, FALSE);
  flatpak_transaction_set_disable_related(transaction, FALSE);

  // connect all transaction signals
  g_signal_connect(transaction, "new-operation", G_CALLBACK(OnNewOperation),
                   self.get());
  g_signal_connect(transaction, "operation-done",
                   G_CALLBACK(OnOperationComplete), self.get());
  g_signal_connect(transaction, "operation-error", G_CALLBACK(OnOperationError),
                   self.get());
  g_signal_connect(transaction, "ready", G_CALLBACK(OnTransactionReady),
                   self.get());

  // Add all extensions to the transaction
  for (const auto& ext : missing_extensions) {
    flatpak_transaction_add_install(transaction, remote.c_str(), ext.c_str(),
                                    nullptr, &error);

    if (error) {
      spdlog::error("[FlatpakPlugin] Failed to add extension '{}': {}", ext,
                    error->message);
      g_clear_error(&error);
      continue;
    }
  }

  // Run transaction in separate thread
  std::thread([transaction, strand_ptr = &strand, complete_callback,
               count = missing_extensions.size(), self]() {
    pthread_setname_np(pthread_self(), "flatpak-ext");

    spdlog::info(
        "[FlatpakPlugin] Extension installation thread started for {} "
        "extensions",
        count);

    GError* error = nullptr;
    gboolean success = flatpak_transaction_run(transaction, nullptr, &error);

    std::string error_message;
    if (error) {
      error_message = error->message;
      spdlog::error("[FlatpakPlugin] Extension installation error: {}",
                    error_message);
      g_clear_error(&error);
    }

    // Post the result back to the original strand
    asio::post(
        *strand_ptr, [complete_callback, success, error_message, count]() {
          if (success) {
            spdlog::info("[FlatpakPlugin] Successfully installed {} extensions",
                         count);
            complete_callback(ErrorOr<bool>(true));
          } else {
            spdlog::error("[FlatpakPlugin] Extension installation failed: {}",
                          error_message);
            complete_callback(ErrorOr<bool>(
                FlutterError("EXTENSION_INSTALL_FAILED", error_message)));
          }
        });

    g_object_unref(transaction);
    spdlog::debug("[FlatpakPlugin] Extension installation thread exiting");
  }).detach();
}

void FlatpakShim::SetupTransactionEventChannel(
    const std::string& id,
    flutter::BinaryMessenger* messenger) {
  std::lock_guard<std::mutex> lock(event_sink_mutex_);
  if (!messenger) {
    spdlog::error(
        "[FlatpakPlugin] Messenger is null, cannot setup event channel");
    return;
  }
  // check for exists channels first
  if (transactions_event_channels_.find(id) !=
      transactions_event_channels_.end()) {
    spdlog::error("[FlatpakPlugin] Messenger channel is already in use : {}",
                  id);
    return;
  }

  try {
    std::string channel_name = "flutter.io/flatpakPlugin/flatpakEvents/" + id;
    auto event_channel =
        std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
            messenger, channel_name,
            &flutter::StandardMethodCodec::GetInstance());

    spdlog::info("[FlatpakPlugin] Channel name: {}", channel_name);

    event_channel->SetStreamHandler(std::make_unique<
                                    flutter::StreamHandlerFunctions<
                                        flutter::EncodableValue>>(
        // onListen callback
        [this, id](
            const flutter::EncodableValue* /* arguments */,
            std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&&
                events)
            -> std::unique_ptr<
                flutter::StreamHandlerError<flutter::EncodableValue>> {
          {
            std::lock_guard<std::mutex> lock(event_sink_mutex_);
            transactions_event_sinks_[id] = std::move(events);
            spdlog::info(
                "[FlatpakPlugin] Event sink connected for transaction:{}", id);
          }

          // Send connection confirmation event
          flutter::EncodableMap test_event;
          test_event[flutter::EncodableValue("type")] =
              flutter::EncodableValue("connection_established");
          test_event[flutter::EncodableValue("message")] =
              flutter::EncodableValue("Event channel ready");
          SendTransactionEvent(id, test_event);

          return nullptr;
        },
        // onCancel callback
        [this, id](const flutter::EncodableValue* /* arguments */)
            -> std::unique_ptr<
                flutter::StreamHandlerError<flutter::EncodableValue>> {
          {
            std::lock_guard<std::mutex> lock(event_sink_mutex_);
            transactions_event_sinks_.erase(id);
            spdlog::info(
                "[FlatpakPlugin] Event sink disconnected for transaction: {}",
                id);
          }
          return nullptr;
        }));
    transactions_event_channels_[id] = std::move(event_channel);
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Exception setting up event channel: {}",
                  e.what());
    return;
  }
}

void FlatpakShim::SetupAccessEventChannel(
    flutter::BinaryMessenger* messenger,
    const asio::io_context::strand& strand) {
  if (!messenger) {
    spdlog::error(
        "[FlatpakPlugin] Messenger is null, cannot setup event channel");
    return;
  }

  if (access_event_channel_) {
    spdlog::error(
        "[FlatpakPlugin] Event channel already exists, "
        "re-registering stream handler");
    return;
  }
  try {
    access_event_channel_ =
        std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
            messenger, "flutter.io/flatpakPlugin/accessEvents",
            &flutter::StandardMethodCodec::GetInstance());

    spdlog::info(
        "[FlatpakPlugin] Channel name: "
        "flutter.io/flatpakPlugin/accessEvents");

    access_event_channel_->SetStreamHandler(
        std::make_unique<
            flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
            // onListen callback
            [this](
                const flutter::EncodableValue* /* arguments */,
                std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&&
                    events)
                -> std::unique_ptr<
                    flutter::StreamHandlerError<flutter::EncodableValue>> {
              {
                std::lock_guard<std::mutex> lock(access_sink_mutex_);
                access_sink_ = std::move(events);
                spdlog::info("[FlatpakPlugin] Event sink connected");
              }
              return nullptr;
            },
            // onCancel callback
            [this](const flutter::EncodableValue* /* arguments */)
                -> std::unique_ptr<
                    flutter::StreamHandlerError<flutter::EncodableValue>> {
              {
                std::lock_guard<std::mutex> lock(access_sink_mutex_);
                access_sink_ = nullptr;
                spdlog::info("[FlatpakPlugin] Event sink disconnected");
              }
              return nullptr;
            }));

    spdlog::info("[FlatpakPlugin] Access portal created and initialized");
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Exception setting up event channel: {}",
                  e.what());
    return;
  }
}

void FlatpakShim::SendTransactionEvent(const std::string& id,
                                       flutter::EncodableMap& event) const {
  std::lock_guard<std::mutex> lock(event_sink_mutex_);

  auto it = transactions_event_sinks_.find(id);
  if (it == transactions_event_sinks_.end() || !it->second) {
    spdlog::error("[FlatpakPlugin] Cannot send event for transaction: {}", id);
    return;
  }

  try {
    event[flutter::EncodableValue("transaction_id")] =
        flutter::EncodableValue(id);
    it->second->Success(flutter::EncodableValue(event));

    if (event.find(flutter::EncodableValue("type")) != event.end()) {
      auto type_value = event[flutter::EncodableValue("type")];
      if (std::holds_alternative<std::string>(type_value)) {
        spdlog::debug("[FlatpakPlugin] Event sent: {}",
                      std::get<std::string>(type_value));
      }
    }
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Exception sending event: {}", e.what());
  }
}

void FlatpakShim::RemoveTransactionEvent(const std::string& id) {
  std::lock_guard<std::mutex> lock(event_sink_mutex_);

  transactions_event_sinks_.erase(id);
  transactions_event_channels_.erase(id);
  spdlog::debug("[FlatpakPlugin] Channel cl: {}", id);
}

void FlatpakShim::SendPermissionEvent(
    const flutter::EncodableMap& event,
    const std::function<void(bool)>& callback) {
  if (!access_sink_) {
    spdlog::error(
        "[FlatpakPlugin] Cannot send event - sink is null. "
        "Flutter may not be listening yet.");
    if (event.count(flutter::EncodableValue("request_id"))) {
      auto request_id = std::get_if<std::string>(
          &event.at(flutter::EncodableValue("request_id")));
      auto permission = std::get_if<std::string>(
          &event.at(flutter::EncodableValue("permission")));

      if (request_id && permission) {
        HandlePermissionResponse(*request_id, *permission, false);
      }
    }
    if (callback)
      callback(false);
    return;
  }

  try {
    access_sink_->Success(flutter::EncodableValue(event));
    if (callback)
      callback(true);
  } catch (const std::exception& e) {
    spdlog::error("[FlatpakPlugin] Exception sending event: {}", e.what());
    if (event.count(flutter::EncodableValue("request_id"))) {
      auto request_id = std::get_if<std::string>(
          &event.at(flutter::EncodableValue("request_id")));
      auto permission = std::get_if<std::string>(
          &event.at(flutter::EncodableValue("permission")));

      if (request_id && permission) {
        HandlePermissionResponse(*request_id, *permission, false);
      }
    }
    if (callback)
      callback(false);
  }
}

void FlatpakShim::OnProgressChanged(FlatpakTransactionProgress* progress,
                                    gpointer user_data) {
  auto* handler = static_cast<FlatpakShim*>(user_data);
  const char* id = static_cast<const char*>(
      g_object_get_data(G_OBJECT(progress), "transaction_id"));

  auto is_estimating = flatpak_transaction_progress_get_is_estimating(progress);
  auto percentage = flatpak_transaction_progress_get_progress(progress);
  auto status = flatpak_transaction_progress_get_status(progress);
  auto bytes_transfered =
      flatpak_transaction_progress_get_bytes_transferred(progress);
  auto start_time = flatpak_transaction_progress_get_start_time(progress);

  std::cout << "\r";
  std::cout << "Progress: " << percentage << "%";

  if (status && strlen(status) > 0) {
    std::cout << " | Status: " << status;
  }

  if (bytes_transfered > 0) {
    double mb_transferred =
        static_cast<double>(bytes_transfered) / (1024.0 * 1024.0);
    std::cout << " | Downloaded: " << std::fixed << std::setprecision(2)
              << mb_transferred << " MB";
  }

  if (start_time > 0 && bytes_transfered > 0) {
    auto current_time = g_get_monotonic_time();
    auto elapsed_time = current_time - start_time;
    if (elapsed_time > 0) {
      auto speed = (static_cast<double>(bytes_transfered) /
                    static_cast<double>(elapsed_time)) *
                   1000000.0;
      double speed_mbps = speed / (1024.0 * 1024.0);
      std::cout << " | Speed: " << std::fixed << std::setprecision(2)
                << speed_mbps << " MB/s";
    }
  }
  std::cout << std::flush;
  if (percentage >= 100) {
    std::cout << std::endl;
  }

  if (handler->strand_) {
    asio::post(*handler->strand_, [id, handler, percentage, is_estimating,
                                   status = status ? std::string(status)
                                                   : std::string(""),
                                   bytes_transfered, start_time]() {
      try {
        flutter::EncodableMap ProgressMap;
        ProgressMap[flutter::EncodableValue("type")] =
            flutter::EncodableValue("progress");
        ProgressMap[flutter::EncodableValue("progress")] =
            flutter::EncodableValue(static_cast<int32_t>(percentage));
        ProgressMap[flutter::EncodableValue("is_estimating")] =
            flutter::EncodableValue(static_cast<bool>(is_estimating));
        ProgressMap[flutter::EncodableValue("status")] =
            flutter::EncodableValue(status);
        ProgressMap[flutter::EncodableValue("bytes")] =
            flutter::EncodableValue(static_cast<int64_t>(bytes_transfered));
        ProgressMap[flutter::EncodableValue("start_time")] =
            flutter::EncodableValue(static_cast<int64_t>(start_time));

        // Calculate speed
        if (start_time > 0 && bytes_transfered > 0) {
          auto current_time = g_get_monotonic_time();
          auto elapsed_time = current_time - start_time;
          if (elapsed_time > 0) {
            auto speed = (static_cast<double>(bytes_transfered) /
                          static_cast<double>(elapsed_time)) *
                         1000000.0;
            ProgressMap[flutter::EncodableValue("speed_bps")] =
                flutter::EncodableValue(speed);
          }
        }

        handler->SendTransactionEvent(id, ProgressMap);
      } catch (const std::exception& e) {
        spdlog::error("[FlatpakPlugin] Error sending progress event: {}",
                      e.what());
      }
    });
  }
}

void FlatpakShim::OnNewOperation(FlatpakTransaction* transaction,
                                 FlatpakTransactionOperation* operation,
                                 FlatpakTransactionProgress* progress,
                                 gpointer user_data) {
  auto* handler = static_cast<FlatpakShim*>(user_data);
  const char* id = static_cast<const char*>(
      g_object_get_data(G_OBJECT(transaction), "transaction_id"));

  if (progress) {
    g_object_set_data_full(G_OBJECT(progress), "transaction_id", g_strdup(id),
                           g_free);
    g_signal_connect(progress, "changed", G_CALLBACK(OnProgressChanged),
                     handler);
  }

  auto operation_ref = flatpak_transaction_operation_get_ref(operation);
  auto operation_type =
      flatpak_transaction_operation_get_operation_type(operation);

  if (handler->strand_) {
    asio::post(
        *handler->strand_,
        [id, handler,
         operation_ref =
             operation_ref ? std::string(operation_ref) : std::string(""),
         operation_type]() {
          try {
            flutter::EncodableMap OperationMap;
            OperationMap[flutter::EncodableValue("type")] =
                flutter::EncodableValue("operation_started");
            OperationMap[flutter::EncodableValue("operation_ref")] =
                flutter::EncodableValue(operation_ref);

            std::string operation_type_str = "unknown";
            switch (operation_type) {
              case FLATPAK_TRANSACTION_OPERATION_INSTALL:
                operation_type_str = "install";
                break;
              case FLATPAK_TRANSACTION_OPERATION_UPDATE:
                operation_type_str = "update";
                break;
              case FLATPAK_TRANSACTION_OPERATION_INSTALL_BUNDLE:
                operation_type_str = "install_bundle";
                break;
              case FLATPAK_TRANSACTION_OPERATION_UNINSTALL:
                operation_type_str = "uninstall";
                break;
              case FLATPAK_TRANSACTION_OPERATION_LAST_TYPE:
                operation_type_str = "last_type";
                break;
              default:
                break;
            }

            OperationMap[flutter::EncodableValue("operation_type")] =
                flutter::EncodableValue(operation_type_str);

            handler->SendTransactionEvent(id, OperationMap);
          } catch (const std::exception& e) {
            spdlog::error("[FlatpakPlugin] Error sending operation event: {}",
                          e.what());
          }
        });
  }
}

void FlatpakShim::OnOperationComplete(FlatpakTransaction* transaction,
                                      FlatpakTransactionOperation* operation,
                                      const char* commit,
                                      FlatpakTransactionResult result,
                                      gpointer user_data) {
  auto* handler = static_cast<FlatpakShim*>(user_data);
  const char* id = static_cast<const char*>(
      g_object_get_data(G_OBJECT(transaction), "transaction_id"));

  const char* ref = flatpak_transaction_operation_get_ref(operation);
  auto type = flatpak_transaction_operation_get_operation_type(operation);

  std::string type_str;
  switch (type) {
    case FLATPAK_TRANSACTION_OPERATION_INSTALL:
      type_str = "install";
      break;
    case FLATPAK_TRANSACTION_OPERATION_UPDATE:
      type_str = "update";
      break;
    case FLATPAK_TRANSACTION_OPERATION_UNINSTALL:
      type_str = "uninstall";
      break;
    default:
      type_str = "other";
      break;
  }

  std::string ref_str = ref ? std::string(ref) : std::string("");

  if (ref) {
    spdlog::info(
        "[FlatpakPlugin] Operation completed: {} {} (result: {})", type_str,
        ref_str,
        result == FLATPAK_TYPE_TRANSACTION_RESULT ? "SUCCESS" : "FAILED");
  }

  std::string app_id;
  auto active_ops = handler->operation_tracker_->GetActiveOperations();

  for (const auto& [id, op_info] : active_ops) {
    if (ref_str.find(id) != std::string::npos || op_info.op_type == type_str) {
      app_id = id;
      handler->operation_tracker_->TrackOperationComplete(app_id, ref_str);
      break;
    }
  }

  if (handler->strand_) {
    asio::post(*handler->strand_, [id, handler, ref_str,
                                   commit = commit ? std::string(commit)
                                                   : std::string(""),
                                   result, type_str, app_id]() {
      try {
        flutter::EncodableMap OperationCompleteMap;
        OperationCompleteMap[flutter::EncodableValue("type")] =
            flutter::EncodableValue("operation_complete");
        OperationCompleteMap[flutter::EncodableValue("operation_ref")] =
            flutter::EncodableValue(ref_str);
        OperationCompleteMap[flutter::EncodableValue("commit")] =
            flutter::EncodableValue(commit);
        OperationCompleteMap[flutter::EncodableValue("success")] =
            flutter::EncodableValue(static_cast<int32_t>(result));
        OperationCompleteMap[flutter::EncodableValue("operation_type")] =
            flutter::EncodableValue(type_str);

        // Determine if this is the main app
        bool is_main_app = ref_str.find("app/") == 0 && !app_id.empty() &&
                           ref_str.find(app_id) != std::string::npos;
        OperationCompleteMap[flutter::EncodableValue("is_main_app")] =
            flutter::EncodableValue(is_main_app);

        handler->SendTransactionEvent(id, OperationCompleteMap);
      } catch (const std::exception& e) {
        spdlog::error("[FlatpakPlugin] Error sending complete event: {}",
                      e.what());
      }
    });
  }
}

gboolean FlatpakShim::OnOperationError(
    FlatpakTransaction* transaction,
    FlatpakTransactionOperation* operation,
    const GError* error,
    FlatpakTransactionErrorDetails error_details,
    gpointer user_data) {
  auto* handler = static_cast<FlatpakShim*>(user_data);
  const char* id = static_cast<const char*>(
      g_object_get_data(G_OBJECT(transaction), "transaction_id"));

  auto operation_ref = flatpak_transaction_operation_get_ref(operation);

  if (handler->strand_) {
    asio::post(
        *handler->strand_,
        [id, handler,
         operation_ref =
             operation_ref ? std::string(operation_ref) : std::string(""),
         error_message = error ? std::string(error->message) : std::string(""),
         error_details]() {
          try {
            flutter::EncodableMap OperationErrorMap;
            OperationErrorMap[flutter::EncodableValue("type")] =
                flutter::EncodableValue("operation_error");
            OperationErrorMap[flutter::EncodableValue("operation_ref")] =
                flutter::EncodableValue(operation_ref);
            OperationErrorMap[flutter::EncodableValue("error_message")] =
                flutter::EncodableValue(error_message);
            OperationErrorMap[flutter::EncodableValue("fatal")] =
                flutter::EncodableValue(
                    !(error_details &
                      FLATPAK_TRANSACTION_ERROR_DETAILS_NON_FATAL));

            handler->SendTransactionEvent(id, OperationErrorMap);
          } catch (const std::exception& e) {
            spdlog::error("[FlatpakPlugin] Error sending error event: {}",
                          e.what());
          }
        });
  }

  return TRUE;
}

gboolean FlatpakShim::OnTransactionReady(FlatpakTransaction* transaction,
                                         gpointer user_data) {
  auto* handler = static_cast<FlatpakShim*>(user_data);
  const char* id = static_cast<const char*>(
      g_object_get_data(G_OBJECT(transaction), "transaction_id"));
  auto* installation = static_cast<FlatpakInstallation*>(
      g_object_get_data(G_OBJECT(transaction), "installation"));

  GList* operations = flatpak_transaction_get_operations(transaction);
  int total_ops = static_cast<int>(g_list_length(operations));

  spdlog::info("[FlatpakPlugin] Total operations to perform: {}", total_ops);

  std::string tx_app_id = id ? id : "";
  guint64 total_download_size = 0;
  bool is_download_transaction = false;
  flutter::EncodableList ops_list;

  for (GList* l = operations; l != nullptr; l = l->next) {
    auto* op = static_cast<FlatpakTransactionOperation*>(l->data);
    const char* ref = flatpak_transaction_operation_get_ref(op);
    FlatpakTransactionOperationType type =
        flatpak_transaction_operation_get_operation_type(op);

    // Sum up the required download size for the app AND all dependencies
    total_download_size += flatpak_transaction_operation_get_download_size(op);

    std::string type_str;
    switch (type) {
      case FLATPAK_TRANSACTION_OPERATION_INSTALL:
        type_str = "install";
        is_download_transaction = true;
        break;
      case FLATPAK_TRANSACTION_OPERATION_UPDATE:
        type_str = "update";
        is_download_transaction = true;
        break;
      case FLATPAK_TRANSACTION_OPERATION_INSTALL_BUNDLE:
        type_str = "install_bundle";
        is_download_transaction = true;
        break;
      case FLATPAK_TRANSACTION_OPERATION_UNINSTALL:
        type_str = "uninstall";
        break;
      default:
        type_str = "unknown";
        break;
    }

    std::string ref_str = ref ? ref : "unknown";
    spdlog::info("[FlatpakPlugin] - Operation: {} {}", type_str, ref_str);

    std::string kind = "unknown";
    if (ref_str.find("app/") == 0) {
      kind = "app";
    } else if (ref_str.find("runtime/") == 0) {
      kind = "runtime";
    }

    flutter::EncodableMap op_map;
    op_map[flutter::EncodableValue("ref")] = flutter::EncodableValue(ref_str);
    op_map[flutter::EncodableValue("type")] = flutter::EncodableValue(type_str);
    op_map[flutter::EncodableValue("kind")] = flutter::EncodableValue(kind);

    ops_list.emplace_back(op_map);
  }

  if (is_download_transaction && installation && !tx_app_id.empty()) {
    if (!handler->check_disk_usage(installation, total_download_size,
                                   tx_app_id)) {
      spdlog::error(
          "[FlatpakPlugin] Transaction aborted natively due to insufficient "
          "disk space.");
      return FALSE;  // Cleanly aborts Flatpak transaction
    }
  }

  handler->operation_tracker_->UpdateTotalOperations(tx_app_id, total_ops);

  if (handler->strand_) {
    asio::post(*handler->strand_, [id, handler, tx_app_id, total_ops,
                                   ops_list = std::move(ops_list)]() {
      try {
        flutter::EncodableMap ready_event;
        ready_event[flutter::EncodableValue("type")] =
            flutter::EncodableValue("transaction_ready");
        ready_event[flutter::EncodableValue("total_operations")] =
            flutter::EncodableValue(total_ops);
        ready_event[flutter::EncodableValue("operations")] =
            flutter::EncodableValue(ops_list);
        ready_event[flutter::EncodableValue("main_app_id")] =
            flutter::EncodableValue(tx_app_id);
        handler->SendTransactionEvent(id, ready_event);
      } catch (const std::exception& e) {
        spdlog::error("[FlatpakPlugin] Error sending ready event: {}",
                      e.what());
      }
    });
  }
  return TRUE;
}

bool FlatpakShim::check_disk_usage(FlatpakInstallation* installation,
                                   guint64 estimated_download,
                                   const std::string& app_id) {
  GError* error = nullptr;
  guint64 min_free_space{0};

  if (!flatpak_installation_get_min_free_space_bytes(installation,
                                                     &min_free_space, &error)) {
    spdlog::error("[FlatpakPlugin] Error getting min free space: {}",
                  error->message);
    g_error_free(error);
    return false;
  }

  // Fix logging bug to show total required space
  guint64 total_requirement = min_free_space + estimated_download;
  spdlog::info(
      "[FlatpakPlugin] Total storage requirement (buffer + download): {} MB",
      total_requirement / 1024 / 1024);

  const auto repo_path = flatpak_installation_get_path(installation);
  auto info = g_file_query_filesystem_info(
      repo_path, G_FILE_ATTRIBUTE_FILESYSTEM_FREE, nullptr, &error);

  if (!info) {
    spdlog::error("[FlatpakPlugin] Error getting free space: {}",
                  error->message);
    g_error_free(error);
    return false;
  }
  const auto available_free_space_bytes =
      g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);

  // Subtract the queue size and current app size mathematically
  uint64_t pending_queue_bytes = operation_tracker_->GetTotalPendingSize();
  uint64_t total_pending_with_this_app =
      pending_queue_bytes + estimated_download;
  uint64_t effective_free_space = available_free_space_bytes;

  if (effective_free_space > total_pending_with_this_app) {
    effective_free_space -= total_pending_with_this_app;
  } else {
    effective_free_space = 0;  // Queue takes up entire drive
  }

  std::string available_free_space_str =
      "Available free space (adjusted for queue): " +
      std::to_string(effective_free_space / 1024 / 1024) + " MB";
  spdlog::info("[FlatpakPlugin] {}", available_free_space_str);

  // check if we have enough space based on the COMPLETE requirement
  if (effective_free_space < total_requirement) {
    spdlog::error("[FlatpakPlugin] Not enough free space!");

    flutter::EncodableMap disk_usage_event;
    disk_usage_event[flutter::EncodableValue("type")] =
        flutter::EncodableValue("free_space_error");
    disk_usage_event[flutter::EncodableValue("available_mb")] =
        flutter::EncodableValue(
            static_cast<int64_t>(effective_free_space / 1024 / 1024));
    disk_usage_event[flutter::EncodableValue("required_mb")] =
        flutter::EncodableValue(
            static_cast<int64_t>(total_requirement / 1024 / 1024));
    disk_usage_event[flutter::EncodableValue("message")] =
        flutter::EncodableValue(
            "Not enough disk space. Available: " +
            std::to_string(effective_free_space / 1024 / 1024) +
            " MB, Required: " +
            std::to_string(total_requirement / 1024 / 1024) + " MB");

    // Use app_id to send the event to the correct flutter channel
    SendTransactionEvent(app_id, disk_usage_event);

    // Clear the operation from the tracker if it fails so it doesn't get stuck
    // in the queue
    operation_tracker_->ClearOperation(app_id);
    return false;
  }

  operation_tracker_->SetOperationSize(app_id, estimated_download);
  return true;
}

std::string FlatpakShim::GenerateRequestId() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();
  return "req_" + std::to_string(ms);
}

void FlatpakShim::CheckExistingPermissions(
    const std::string& app_id,
    const std::vector<std::string>& permissions,
    const std::function<void(std::map<std::string, bool>)>& callback) const {
  if (permissions.empty()) {
    callback({});
    return;
  }

  permissions_portal_->CheckAllPermissions(
      app_id, permissions,
      [callback](const std::map<std::string, flatpak_plugin::PermissionStatus>&
                     statuses) {
        std::map<std::string, bool> results;
        for (const auto& [perm, status] : statuses) {
          switch (status) {
            case flatpak_plugin::PermissionStatus::GRANTED:
              results[perm] = true;
              break;
            case flatpak_plugin::PermissionStatus::DENIED:
              results[perm] = false;
              break;
            case flatpak_plugin::PermissionStatus::ASK:
            case flatpak_plugin::PermissionStatus::NOT_SET:
              // Don't add to results
              break;
          }
        }
        callback(results);
      });
}

void FlatpakShim::ShowNextDialog(const std::string& request_id) {
  flutter::EncodableMap event;

  std::string app_id;
  std::string current_permission;
  size_t permission_index;
  size_t total_requests;
  bool should_continue = false;

  {
    std::lock_guard<std::mutex> lock(permissions_mutex_);
    auto it = active_permissions_.find(request_id);
    if (it == active_permissions_.end()) {
      spdlog::error(
          "[FlatpakPlugin] Failed to retrieve {} request for permission",
          request_id);
      return;
    }

    PermissionRequest& request = it->second;

    // Check if all permissions processed
    if (request.permission_index >= request.requests.size()) {
      spdlog::debug("[FlatpakPlugin] All Permissions processed for {}",
                    request.app_id);
      auto callback = request.callback;
      auto result = request.result;
      active_permissions_.erase(it);

      lock.~lock_guard();
      callback(result);
      return;
    }

    // Get current permission info
    app_id = request.app_id;
    current_permission = request.requests[request.permission_index];
    permission_index = request.permission_index;
    total_requests = request.requests.size();
    should_continue = true;
  }

  if (!should_continue)
    return;

  event[flutter::EncodableValue("type")] =
      flutter::EncodableValue("permission_dialog");
  event[flutter::EncodableValue("request_id")] =
      flutter::EncodableValue(request_id);
  event[flutter::EncodableValue("app_id")] = flutter::EncodableValue(app_id);
  event[flutter::EncodableValue("permission")] =
      flutter::EncodableValue(current_permission);
  event[flutter::EncodableValue("progress")] =
      flutter::EncodableValue(static_cast<int>(permission_index + 1));
  event[flutter::EncodableValue("total")] =
      flutter::EncodableValue(static_cast<int>(total_requests));
  event[flutter::EncodableValue("timestamp")] = flutter::EncodableValue(
      std::chrono::system_clock::now().time_since_epoch().count());

  spdlog::debug("[FlatpakPlugin] Showing permission Dialog : {} {}/{}",
                current_permission, permission_index + 1, total_requests);

  SendPermissionEvent(
      event, [weak_self = weak_from_this(), request_id](bool success) {
        if (!success) {
          auto self = weak_self.lock();
          if (!self)
            return;

          spdlog::error("[FlatpakPlugin] Failed to show dialog");

          std::lock_guard<std::mutex> lock(self->permissions_mutex_);
          auto it = self->active_permissions_.find(request_id);
          if (it != self->active_permissions_.end()) {
            auto callback = it->second.callback;
            self->active_permissions_.erase(it);

            lock.~lock_guard();
            callback({});
          }
        }
      });
}

void FlatpakShim::HandlePermissionResponse(const std::string& request_id,
                                           const std::string& permission,
                                           bool granted) {
  spdlog::debug("[FlatpakPlugin] Permission response : {} -> {}", permission,
                granted ? "granted" : "denied");
  std::string table = "devices";
  if (permission == "location") {
    table = "location";
  } else if (permission == "notifications") {
    table = "notifications";
  } else if (permission == "background") {
    table = "background";
  }

  // Retrieve app id from requests
  std::string app_id;
  {
    std::lock_guard<std::mutex> lock(permissions_mutex_);
    auto it = active_permissions_.find(request_id);
    if (it != active_permissions_.end()) {
      app_id = it->second.app_id;
      it->second.result[permission] = granted;
      it->second.permission_index++;
    }
  }

  if (!app_id.empty()) {
    std::string table = "devices";
    std::string resource_id = permission;
    if (permission == "location") {
      table = "location";
    } else if (permission == "notifications") {
      table = "notifications";
    } else if (permission == "background") {
      table = "background";
      resource_id = app_id;
      granted = false;
    }

    permissions_portal_->SetPermission(
        table, permission, app_id, {granted ? "yes" : "no"},
        [weak_self = weak_from_this(), request_id, permission,
         granted](bool success) {
          auto self = weak_self.lock();
          if (!self)
            return;

          if (success) {
            spdlog::debug("[FlatpakPlugin] Stored: {} -> {}", permission,
                          granted ? "granted" : "denied");
          } else {
            spdlog::error("[FlatpakPlugin] Failed to store permission");
          }

          // Show next dialog
          self->ShowNextDialog(request_id);
        });
  }
}

void FlatpakShim::RequestAppLaunchPermission(
    const std::string& app_id,
    FlatpakInstalledRef* installed_ref,
    const std::function<void(const std::map<std::string, bool>&)>& callback) {
  if (!permissions_portal_) {
    spdlog::error("[FlatpakPlugin] Access portal not initialized");
    callback({});
    return;
  }
  const std::string metadata = get_metadata_as_string(installed_ref);
  const FlatpakShim::sandbox sandbox = parse_metadata(metadata);
  std::vector<std::string> requested_permissions;

  for (const auto& dev : sandbox.context.devices) {
    if (dev == "all") {
      requested_permissions.emplace_back("microphone");
      requested_permissions.emplace_back("speakers");
      requested_permissions.emplace_back("camera");
      break;
    }
  }

  for (const auto& sock : sandbox.context.sockets) {
    if (sock == "pulseaudio") {
      auto it = std::find(requested_permissions.begin(),
                          requested_permissions.end(), "microphone");
      if (it == requested_permissions.end()) {
        requested_permissions.emplace_back("microphone");
        requested_permissions.emplace_back("speakers");
      }
      break;
    }
  }
  for (const auto& bus : sandbox.session_bus) {
    if (bus == "org.freedesktop.Notifications") {
      requested_permissions.emplace_back("notifications");
    } else if (bus == "org.freedesktop.portal.Usb") {
      requested_permissions.emplace_back("usb");
    } else if (bus == "org.freedesktop.portal.Location") {
      requested_permissions.emplace_back("location");
    } else if (bus == "org.freedesktop.portal.Camera") {
      auto it = std::find(requested_permissions.begin(),
                          requested_permissions.end(), "camera");
      if (it == requested_permissions.end()) {
        requested_permissions.emplace_back("camera");
      }
    }
  }

  if (requested_permissions.empty()) {
    spdlog::info("[FlatpakPlugin] No permissions needed for app : {}", app_id);
    callback({});
    return;
  }

  spdlog::info("[FlatpakPlugin] Check Existing permissions for app : {}",
               app_id);

  CheckExistingPermissions(
      app_id, requested_permissions,
      [weak_self = weak_from_this(), app_id, requested_permissions,
       callback](std::map<std::string, bool> permissions) {
        auto self = weak_self.lock();
        if (!self) {
          callback({});
          return;
        }

        std::vector<std::string> permissions_to_ask;
        std::map<std::string, bool> final_results = permissions;

        for (const auto& perm : requested_permissions) {
          if (permissions.find(perm) == permissions.end()) {
            permissions_to_ask.emplace_back(perm);
          }
        }

        if (permissions_to_ask.empty()) {
          spdlog::info(
              "[FlatpakPlugin] No permissions needed to ask for app : {}",
              app_id);
          callback(final_results);
          return;
        }

        std::string request_id =
            flatpak_plugin::FlatpakShim::GenerateRequestId();

        PermissionRequest request;
        request.app_id = app_id;
        request.result = final_results;
        request.permission_index = 0;
        request.requests = permissions_to_ask;
        request.callback = callback;

        {
          std::lock_guard<std::mutex> lock(self->permissions_mutex_);
          self->active_permissions_[request_id] = std::move(request);
        }

        spdlog::info(
            "[FlatpakPlugin] Starting Permissions dialog for {} permission",
            permissions_to_ask.size());
        self->ShowNextDialog(request_id);
      });
}

void FlatpakShim::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  asio::post(*strand_, [weak_self = weak_from_this(),
                        method_name = method_call.method_name(),
                        arguments = method_call.arguments()
                                        ? *method_call.arguments()
                                        : flutter::EncodableValue(),
                        result = std::move(result)]() mutable {
    auto self = weak_self.lock();
    if (!self) {
      result->Error("CANCELLED", "Operation cancelled");
      return;
    }
    const auto* args = std::get_if<flutter::EncodableMap>(&arguments);

    if (method_name == "permissionResponse") {
      if (!args) {
        result->Error("INVALID_ARGUMENT", "argument is not map");
        return;
      }
      auto request_id_it = args->find(flutter::EncodableValue("request_id"));
      auto permission_it = args->find(flutter::EncodableValue("permission"));
      auto granted_it = args->find(flutter::EncodableValue("granted"));
      if (request_id_it == args->end() || permission_it == args->end() ||
          granted_it == args->end()) {
        result->Error("MISSING_FIELDS",
                      "request_id ,permission or granted is missing");
        return;
      }

      auto* request_id = std::get_if<std::string>(&request_id_it->second);
      auto* permission = std::get_if<std::string>(&permission_it->second);
      auto* granted = std::get_if<bool>(&granted_it->second);

      if (!request_id || !permission || !granted) {
        result->Error("INVALID_TYPES", "invalid arguments types");
        return;
      }
      self->HandlePermissionResponse(*request_id, *permission, *granted);
      result->Success();
      return;
    }
    // TODO: Handle other methods.
    if (method_name == "checkPermissions") {
      result->NotImplemented();
      return;
    }

    if (method_name == "grantPermission") {
      result->NotImplemented();
      return;
    }

    if (method_name == "revokePermission") {
      result->NotImplemented();
      return;
    }
    result->NotImplemented();
  });
}
}  // namespace flatpak_plugin
