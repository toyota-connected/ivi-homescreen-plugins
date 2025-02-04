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

#include "flatpak_plugin.h"

#include <filesystem>
#include <sstream>
#include <vector>

#include <zlib.h>
#include <asio/post.hpp>

#include "appstream_catalog.h"
#include "common.h"
#include "messages.g.h"
#include "plugins/common/common.h"

namespace flatpak_plugin {

constexpr size_t BUFFER_SIZE = 32768;

// static
void FlatpakPlugin::RegisterWithRegistrar(flutter::PluginRegistrar* registrar) {
  auto plugin = std::make_unique<FlatpakPlugin>();

  SetUp(registrar->messenger(), plugin.get());

  registrar->AddPlugin(std::move(plugin));
}

FlatpakPlugin::FlatpakPlugin()
    : io_context_(std::make_unique<asio::io_context>(ASIO_CONCURRENCY_HINT_1)),
      work_(io_context_->get_executor()),
      strand_(std::make_unique<asio::io_context::strand>(*io_context_)) {
  thread_ = std::thread([&] { io_context_->run(); });

  asio::post(*strand_, [&]() {
    pthread_self_ = pthread_self();
    spdlog::debug("\tthread_id=0x{:x}", pthread_self_);
  });

  spdlog::debug("[FlatpakPlugin]");
  spdlog::debug("\tlinked with libflatpak.so v{}.{}.{}", FLATPAK_MAJOR_VERSION,
                FLATPAK_MINOR_VERSION, FLATPAK_MICRO_VERSION);
  spdlog::debug("\tDefault Arch: {}", flatpak_get_default_arch());
  spdlog::debug("\tSupported Arches:");
  if (auto* supported_arches = flatpak_get_supported_arches()) {
    for (auto arch = supported_arches; *arch != nullptr; ++arch) {
      spdlog::debug("\t\t{}", *arch);
    }
  }
}

FlatpakPlugin::~FlatpakPlugin() = default;

// Get Flatpak Version
ErrorOr<std::string> FlatpakPlugin::GetVersion() {
  std::stringstream ss;
  ss << FLATPAK_MAJOR_VERSION << "." << FLATPAK_MINOR_VERSION << "."
     << FLATPAK_MICRO_VERSION;
  return ss.str();
}

// Get the default flatpak arch
ErrorOr<std::string> FlatpakPlugin::GetDefaultArch() {
  std::string default_arch = flatpak_get_default_arch();
  return default_arch;
}

// Get all arches supported by flatpak
ErrorOr<flutter::EncodableList> FlatpakPlugin::GetSupportedArches() {
  flutter::EncodableList result;
  if (auto* supported_arches = flatpak_get_supported_arches()) {
    for (auto arch = supported_arches; *arch != nullptr; ++arch) {
      result.emplace_back(static_cast<const char*>(*arch));
    }
  }
  return result;
}

GPtrArray* FlatpakPlugin::get_system_installations() {
  GError* error = nullptr;
  const auto sys_installs = flatpak_get_system_installations(nullptr, &error);
  if (error) {
    spdlog::error("[FlatpakPlugin] Error getting system installations: {}",
                  error->message);
    g_clear_error(&error);
  }

  return sys_installs;
}

GPtrArray* FlatpakPlugin::get_remotes(FlatpakInstallation* installation) {
  GError* error = nullptr;
  const auto remotes =
      flatpak_installation_list_remotes(installation, nullptr, &error);

  if (error) {
    spdlog::error("[FlatpakPlugin] Error listing remotes: {}", error->message);
    g_clear_error(&error);
  }
  return remotes;
}

flutter::EncodableList FlatpakPlugin::installation_get_default_languages(
    FlatpakInstallation* installation) {
  flutter::EncodableList languages;
  GError* error = nullptr;

  const auto default_languages =
      flatpak_installation_get_default_languages(installation, &error);
  if (error) {
    spdlog::error(
        "[FlatpakPlugin] flatpak_installation_get_default_languages: {}",
        error->message);
    g_error_free(error);
    error = nullptr;
  }
  if (default_languages != nullptr) {
    for (auto language = default_languages; *language != nullptr; ++language) {
      languages.emplace_back(static_cast<const char*>(*language));
    }
    g_strfreev(default_languages);
  }
  return languages;
}

flutter::EncodableList FlatpakPlugin::installation_get_default_locales(
    FlatpakInstallation* installation) {
  flutter::EncodableList locales;
  GError* error = nullptr;

  auto default_locales =
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

void format_time_iso8601(time_t raw_time, char* buffer, size_t buffer_size) {
  // Convert raw time to local time
  tm tm_info{};
  localtime_r(&raw_time, &tm_info);

  // Format the time part
  strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%S", &tm_info);

  // Get timezone offset in seconds
  long timezone_offset = tm_info.tm_gmtoff;
  const char sign = (timezone_offset >= 0) ? '+' : '-';
  timezone_offset = (timezone_offset >= 0) ? timezone_offset : -timezone_offset;

  // Calculate hours and minutes
  const int hours = static_cast<int>(timezone_offset / 3600);
  const int minutes = static_cast<int>((timezone_offset % 3600) / 60);

  // Append timezone offset to the buffer
  char tz_buffer[7];  // To store the timezone offset in +-HH:MM format
  snprintf(tz_buffer, sizeof(tz_buffer), "%c%02d:%02d", sign, hours, minutes);
  strncat(buffer, tz_buffer, buffer_size - strlen(buffer) - 1);
}

Installation FlatpakPlugin::get_installation(
    FlatpakInstallation* installation) {
  flutter::EncodableList remote_list;

  const auto remotes = get_remotes(installation);
  for (size_t j = 0; j < remotes->len; j++) {
    const auto remote =
        static_cast<FlatpakRemote*>(g_ptr_array_index(remotes, j));

    const auto name = flatpak_remote_get_name(remote);
    const auto url = flatpak_remote_get_url(remote);
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
    int32_t prio = flatpak_remote_get_prio(remote);

    const auto default_arch = flatpak_get_default_arch();
    auto appstream_timestamp_path = g_file_get_path(
        flatpak_remote_get_appstream_timestamp(remote, default_arch));
    auto appstream_dir_path =
        g_file_get_path(flatpak_remote_get_appstream_dir(remote, default_arch));

    std::filesystem::path appstream_xml_path = appstream_dir_path;
    appstream_xml_path /= "appstream.xml";
    auto catalog = AppstreamCatalog(appstream_xml_path, "");
    spdlog::debug("[FlatpakPlugin] Appstream Catalog Total components: {}",
                  catalog.getTotalComponentCount());
    const auto& components = catalog.getComponents();
    for (const auto& component : components) {
      PrintComponent(component);
    }

    auto appstream_timestamp =
        get_appstream_timestamp(appstream_timestamp_path);
    char formatted_time[30];
    format_time_iso8601(appstream_timestamp, formatted_time,
                        sizeof(formatted_time));

    remote_list.emplace_back(flutter::CustomEncodableValue(Remote(
        name ? name : "", url ? url : "", collection_id ? collection_id : "",
        title ? title : "", comment ? comment : "",
        description ? description : "", homepage ? homepage : "",
        icon ? icon : "", default_branch ? default_branch : "",
        main_ref ? main_ref : "",
        FlatpakRemoteTypeToString(flatpak_remote_get_remote_type(remote)),
        filter ? filter : "", formatted_time, appstream_dir_path, gpg_verify,
        no_enumerate, no_deps, disabled, prio)));
  }
  g_ptr_array_unref(remotes);

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

  return Installation(id, display_name, path, no_interaction, is_user, priority,
                      default_languages, default_locales, remote_list);
}

// Get configuration of user installation.
ErrorOr<Installation> FlatpakPlugin::GetUserInstallation() {
  GError* error = nullptr;
  const auto installation = flatpak_installation_new_user(nullptr, &error);
  return get_installation(installation);
}

ErrorOr<flutter::EncodableList> FlatpakPlugin::GetSystemInstallations() {
  flutter::EncodableList installs_list;
  const auto system_installations = get_system_installations();
  for (size_t i = 0; i < system_installations->len; i++) {
    const auto installation = static_cast<FlatpakInstallation*>(
        g_ptr_array_index(system_installations, i));
    installs_list.emplace_back(
        flutter::CustomEncodableValue(get_installation(installation)));
  }
  g_ptr_array_unref(system_installations);

  return installs_list;
}

ErrorOr<bool> FlatpakPlugin::RemoteAdd(const Remote& /* configuration */) {
  spdlog::info("[FlatpakPlugin] Not Implemented: {}", __FUNCTION__);

  return true;
}

ErrorOr<bool> FlatpakPlugin::RemoteRemove(const std::string& /* id */) {
  spdlog::info("[FlatpakPlugin] Not Implemented: {}", __FUNCTION__);
  return true;
}

void FlatpakPlugin::get_application_list(
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
    g_object_unref(installation);
    return;
  }

  for (guint i = 0; i < refs->len; i++) {
    const auto ref =
        static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

    const auto appdata_name = flatpak_installed_ref_get_appdata_name(ref);
    const auto appdata_id = flatpak_installation_get_id(installation);
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

    flutter::EncodableList subpath_list;
    auto subpaths = flatpak_installed_ref_get_subpaths(ref);
    if (subpaths != nullptr) {
      for (auto sub_path = subpaths; *sub_path != nullptr; ++sub_path) {
        subpath_list.emplace_back(static_cast<const char*>(*sub_path));
      }
    }

    application_list.emplace_back(flutter::CustomEncodableValue(Application(
        appdata_name ? appdata_name : "", appdata_id ? appdata_id : "",
        appdata_summary ? appdata_summary : "",
        appdata_version ? appdata_version : "",
        appdata_origin ? appdata_origin : "",
        appdata_license ? appdata_license : "",
        static_cast<int64_t>(flatpak_installed_ref_get_installed_size(ref)),
        deploy_dir ? deploy_dir : "", flatpak_installed_ref_get_is_current(ref),
        content_rating_type ? content_rating_type : "",
        get_content_rating_map(ref), latest_commit ? latest_commit : "",
        eol ? eol : "", eol_rebase ? eol_rebase : "", subpath_list,
        get_metadata_as_string(ref), get_appdata_as_string(ref))));
  }
  g_ptr_array_unref(refs);
}

ErrorOr<flutter::EncodableList> FlatpakPlugin::GetApplicationsInstalled() {
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

  return application_list;
}

ErrorOr<flutter::EncodableList> FlatpakPlugin::GetApplicationsRemote(
    const std::string& /* id */) {
  spdlog::info("[FlatpakPlugin] Not Implemented: {}", __FUNCTION__);
  return flutter::EncodableList();
}

ErrorOr<bool> FlatpakPlugin::ApplicationInstall(const std::string& /* id */) {
  spdlog::info("[FlatpakPlugin] Not Implemented: {}", __FUNCTION__);
  return true;
}

ErrorOr<bool> FlatpakPlugin::ApplicationUninstall(const std::string& /* id */) {
  spdlog::info("[FlatpakPlugin] Not Implemented: {}", __FUNCTION__);
  return true;
}

ErrorOr<bool> FlatpakPlugin::ApplicationStart(
    const std::string& /* id */,
    const flutter::EncodableMap* /* configuration */) {
  spdlog::info("[FlatpakPlugin] Not Implemented: {}", __FUNCTION__);
  return true;
}

ErrorOr<bool> FlatpakPlugin::ApplicationStop(const std::string& /* id */) {
  spdlog::info("[FlatpakPlugin] Not Implemented: {}", __FUNCTION__);
  return true;
}

std::time_t FlatpakPlugin::get_appstream_timestamp(
    const std::filesystem::path& timestamp_filepath) {
  if (exists(timestamp_filepath)) {
    std::filesystem::file_time_type fileTime =
        std::filesystem::last_write_time(timestamp_filepath);

    // return system time
    const auto sctp =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            fileTime - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sctp);
  }
  spdlog::error("[FlatpakPlugin] appstream_timestamp does not exist: {}",
                timestamp_filepath.c_str());
  return {};
}

std::vector<char> FlatpakPlugin::decompress_gzip(
    const std::vector<char>& compressedData,
    std::vector<char>& decompressedData) {
  z_stream zs = {};

  if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
    spdlog::error("[FlatpakPlugin] Unable to initialize zlib inflate");
    return {};
  }

  zs.next_in =
      const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressedData.data()));
  zs.avail_in = static_cast<uInt>(compressedData.size());

  int zlibResult;
  const auto buffer = std::make_unique<char[]>(BUFFER_SIZE);

  do {
    zs.next_out = reinterpret_cast<Bytef*>(buffer.get());
    zs.avail_out = BUFFER_SIZE;

    zlibResult = inflate(&zs, 0);

    if (decompressedData.size() < zs.total_out) {
      decompressedData.insert(
          decompressedData.end(), buffer.get(),
          buffer.get() + zs.total_out - decompressedData.size());
    }
  } while (zlibResult == Z_OK);

  inflateEnd(&zs);

  if (zlibResult != Z_STREAM_END) {
    spdlog::error("[FlatpakPlugin] Gzip decompression error");
    return {};
  }

  return decompressedData;
}

std::string FlatpakPlugin::get_metadata_as_string(
    FlatpakInstalledRef* installed_ref) {
  GError* error = nullptr;

  const auto g_bytes =
      flatpak_installed_ref_load_metadata(installed_ref, nullptr, &error);

  if (!g_bytes) {
    if (error != nullptr) {
      spdlog::error("[FlatpakPlugin] Error loading metadata: %s\n",
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

std::string FlatpakPlugin::get_appdata_as_string(
    FlatpakInstalledRef* installed_ref) {
  GError* error = nullptr;
  const auto g_bytes =
      flatpak_installed_ref_load_appdata(installed_ref, nullptr, &error);
  if (!g_bytes) {
    if (error != nullptr) {
      SPDLOG_ERROR("[FlatpakPlugin] {}", error->message);
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

flutter::EncodableMap FlatpakPlugin::get_content_rating_map(
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
    result[flutter::EncodableValue(static_cast<char*>(key))] =
        flutter::EncodableValue(static_cast<char*>(value));
  }
  return result;
}

}  // namespace flatpak_plugin