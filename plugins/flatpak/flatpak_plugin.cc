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
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/tree.h>
#include <libxml2/libxml/xpath.h>
#include <zlib.h>
#include <asio/post.hpp>

#include "inipp.h"
#include "messages.h"
#include "plugins/common/common.h"

namespace flatpak_plugin {

static constexpr char kIconPathSuffix[] =
    "files/share/app-info/icons/flatpak/64x64";
static constexpr char kIconXpathQuery[] =
    "//components[1]/component[1]/icon[@type='cached' and @height='64' and "
    "@width='64']";

constexpr size_t BUFFER_SIZE = 32768;

// static
void FlatpakPlugin::RegisterWithRegistrar(flutter::PluginRegistrar* registrar) {
  auto plugin = std::make_unique<FlatpakPlugin>();

  SetUp(registrar->messenger(), plugin.get());

  registrar->AddPlugin(std::move(plugin));
}

flutter::EncodableList FlatpakPlugin::GetApplicationList(
    FlatpakInstallation* installation) {
  flutter::EncodableList result;
  GError* error = nullptr;
  GCancellable* cancellable = nullptr;

  // List all installed refs
  cancellable = g_cancellable_new();
  auto refs = flatpak_installation_list_installed_refs(installation,
                                                       cancellable, &error);
  g_cancellable_cancel(cancellable);
  g_object_unref(cancellable);

  if (error) {
    spdlog::error("[FlatpakPlugin] Error listing installed refs: {}",
                  error->message);
    g_clear_error(&error);
    g_object_unref(installation);
    return {};
  }

  for (guint i = 0; i < refs->len; i++) {
    auto ref = static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));

    flutter::EncodableMap map;

    auto appdata_name = flatpak_installed_ref_get_appdata_name(ref);
    map.emplace(flutter::EncodableValue("appdata_name"),
                flutter::EncodableValue(
                    appdata_name ? appdata_name
                                 : flatpak_installation_get_id(installation)));

    map.emplace(flutter::EncodableValue("application_id"),
                flutter::EncodableValue(get_application_id(ref)));

    auto appdata_summary = flatpak_installed_ref_get_appdata_summary(ref);
    map.emplace(
        flutter::EncodableValue("appdata_summary"),
        flutter::EncodableValue(appdata_summary ? appdata_summary : ""));

    auto appdata_version = flatpak_installed_ref_get_appdata_version(ref);
    map.emplace(
        flutter::EncodableValue("appdata_version"),
        flutter::EncodableValue(appdata_version ? appdata_version : ""));

    auto appdata_origin = flatpak_installed_ref_get_origin(ref);
    map.emplace(flutter::EncodableValue("appdata_origin"),
                flutter::EncodableValue(appdata_origin ? appdata_origin : ""));

    auto appdata_license = flatpak_installed_ref_get_appdata_license(ref);
    map.emplace(
        flutter::EncodableValue("appdata_license"),
        flutter::EncodableValue(appdata_license ? appdata_license : ""));

    // convert back to BigInt in Dart.  e.g. var x = new BigInt.from(5);
    auto installed_size =
        static_cast<int64_t>(flatpak_installed_ref_get_installed_size(ref));
    map.emplace(flutter::EncodableValue("installed_size"),
                flutter::EncodableValue(installed_size));

    auto deploy_dir = flatpak_installed_ref_get_deploy_dir(ref);
    map.emplace(flutter::EncodableValue("deploy_dir"),
                flutter::EncodableValue(deploy_dir ? deploy_dir : ""));

    parse_appstream_xml(ref, deploy_dir);

    auto is_current = flatpak_installed_ref_get_is_current(ref);
    map.emplace(flutter::EncodableValue("is_current"),
                flutter::EncodableValue(is_current));

    auto content_rating_type =
        flatpak_installed_ref_get_appdata_content_rating_type(ref);
    map.emplace(flutter::EncodableValue("content_rating_type"),
                flutter::EncodableValue(
                    content_rating_type ? content_rating_type : ""));

    auto latest_commit = flatpak_installed_ref_get_latest_commit(ref);
    map.emplace(flutter::EncodableValue("latest_commit"),
                flutter::EncodableValue(latest_commit ? latest_commit : ""));

    auto eol = flatpak_installed_ref_get_eol(ref);
    map.emplace(flutter::EncodableValue("eol"),
                flutter::EncodableValue(eol ? eol : ""));

    auto eol_rebase = flatpak_installed_ref_get_eol_rebase(ref);
    map.emplace(flutter::EncodableValue("eol_rebase"),
                flutter::EncodableValue(eol_rebase ? eol_rebase : ""));

    flutter::EncodableList subpath_list;
    auto subpaths = flatpak_installed_ref_get_subpaths(ref);
    if (subpaths != nullptr) {
      for (auto sub_path = subpaths; *sub_path != nullptr; ++sub_path) {
        subpath_list.emplace_back(*sub_path);
      }
      map.emplace(flutter::EncodableValue("subpaths"), std::move(subpath_list));
    }
    result.push_back(
        static_cast<const flutter::EncodableValue>(std::move(map)));
  }

  plugin_common::Encodable::PrintFlutterEncodableList("Apps", result);
  return result;
}

std::time_t get_appstream_timestamp(
    const std::filesystem::path& timestamp_filepath) {
  if (exists(timestamp_filepath)) {
    std::filesystem::file_time_type fileTime =
        std::filesystem::last_write_time(timestamp_filepath);

    // return system time
    auto sctp =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            fileTime - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sctp);
  }
  spdlog::error("[FlatpakPlugin] appstream_timestamp does not exist: {}",
                timestamp_filepath.c_str());
  return {};
}

flutter::EncodableList FlatpakPlugin::GetRemotes(
    FlatpakInstallation* installation,
    const std::string& default_arch) {
  flutter::EncodableList list;
  GError* error = nullptr;
  GCancellable* cancellable = nullptr;

  cancellable = g_cancellable_new();
  auto remotes =
      flatpak_installation_list_remotes(installation, cancellable, &error);
  g_cancellable_cancel(cancellable);
  g_object_unref(cancellable);

  if (error) {
    spdlog::error("[FlatpakPlugin] Error listing remotes: {}", error->message);
    g_clear_error(&error);
  }

  for (auto i = 0; i < remotes->len; i++) {
    flutter::EncodableMap map;
    auto remote = static_cast<FlatpakRemote*>(g_ptr_array_index(remotes, i));

    auto name = flatpak_remote_get_name(remote);

    auto appstream_timestamp_path = g_file_get_path(
        flatpak_remote_get_appstream_timestamp(remote, default_arch.c_str()));
    auto appstream_dir_path = g_file_get_path(
        flatpak_remote_get_appstream_dir(remote, default_arch.c_str()));

    auto url = flatpak_remote_get_url(remote);
    auto collection_id = flatpak_remote_get_collection_id(remote);
    auto title = flatpak_remote_get_title(remote);
    auto comment = flatpak_remote_get_comment(remote);
    auto description = flatpak_remote_get_description(remote);
    auto homepage = flatpak_remote_get_homepage(remote);
    auto icon = flatpak_remote_get_icon(remote);
    auto default_branch = flatpak_remote_get_default_branch(remote);
    auto main_ref = flatpak_remote_get_main_ref(remote);
    bool gpg_verify = flatpak_remote_get_gpg_verify(remote);
    bool no_enumerate = flatpak_remote_get_noenumerate(remote);
    bool no_deps = flatpak_remote_get_nodeps(remote);
    bool disabled = flatpak_remote_get_disabled(remote);
    int32_t prio = flatpak_remote_get_prio(remote);
    auto filter = flatpak_remote_get_filter(remote);

    parse_repo_appstream_xml(appstream_dir_path);

    map.emplace(
        flutter::EncodableValue("name"),
        flutter::EncodableValue(std::move(std::string(name ? name : ""))));
    map.emplace(
        flutter::EncodableValue("url"),
        flutter::EncodableValue(std::move(std::string(url ? url : ""))));
    map.emplace(
        flutter::EncodableValue("title"),
        flutter::EncodableValue(std::move(std::string(title ? title : ""))));
    map.emplace(flutter::EncodableValue("default_branch"),
                flutter::EncodableValue(std::move(
                    std::string(default_branch ? default_branch : ""))));
    map.emplace(flutter::EncodableValue("collection_id"),
                flutter::EncodableValue(std::move(
                    std::string(collection_id ? collection_id : ""))));
    map.emplace(flutter::EncodableValue("comment"),
                flutter::EncodableValue(
                    std::move(std::string(comment ? comment : ""))));
    map.emplace(flutter::EncodableValue("description"),
                flutter::EncodableValue(
                    std::move(std::string(description ? description : ""))));
    map.emplace(flutter::EncodableValue("disabled"),
                flutter::EncodableValue(disabled));
    map.emplace(
        flutter::EncodableValue("filter"),
        flutter::EncodableValue(std::move(std::string(filter ? filter : ""))));
    map.emplace(flutter::EncodableValue("gpg_verify"),
                flutter::EncodableValue(gpg_verify));
    map.emplace(flutter::EncodableValue("homepage"),
                flutter::EncodableValue(
                    std::move(std::string(homepage ? homepage : ""))));
    map.emplace(
        flutter::EncodableValue("icon"),
        flutter::EncodableValue(std::move(std::string(icon ? icon : ""))));
    map.emplace(flutter::EncodableValue("main_ref"),
                flutter::EncodableValue(
                    std::move(std::string(main_ref ? main_ref : ""))));
    map.emplace(flutter::EncodableValue("nodeps"),
                flutter::EncodableValue(no_deps));
    map.emplace(flutter::EncodableValue("noenumerate"),
                flutter::EncodableValue(no_enumerate));
    map.emplace(flutter::EncodableValue("prio"), flutter::EncodableValue(prio));
    map.emplace(flutter::EncodableValue("remote_type"),
                flutter::EncodableValue(FlatpakRemoteTypeToString(
                    flatpak_remote_get_remote_type(remote))));

    auto appstream_timestamp =
        get_appstream_timestamp(appstream_timestamp_path);
    if (appstream_timestamp) {
      std::string str = std::asctime(std::localtime(&appstream_timestamp));
      str.pop_back();
      map.emplace(flutter::EncodableValue("appstream_timestamp"),
                  flutter::EncodableValue(std::move(str)));
    }
    map.emplace(
        flutter::EncodableValue("appstream_dir"),
        flutter::EncodableValue(std::move(std::string(appstream_dir_path))));

    list.push_back(static_cast<const flutter::EncodableValue>(std::move(map)));
  }

  plugin_common::Encodable::PrintFlutterEncodableList("remotes", list);

  return list;
}

void FlatpakPlugin::PrintInstallation(
    const std::unique_ptr<struct installation>& install) {
  spdlog::debug("[FlatpakPlugin]");
  spdlog::debug("\tID: [{}]", install->id.empty() ? "" : install->id);
  spdlog::debug("\tDisplay Name: {}",
                install->display_name.empty() ? "" : install->display_name);
  spdlog::debug("\tPath: [{}]", install->path.c_str());
  spdlog::debug("\tNo Interaction: {}",
                install->no_interaction ? "true" : "false");
  spdlog::debug("\tIs User: {}", install->is_user ? "true" : "false");
  spdlog::debug("\tPriority: {}", install->priority);

  for (const auto& language : install->default_languages) {
    spdlog::debug("\tLanguage: {}", language);
  }
  for (const auto& locale : install->default_locales) {
    spdlog::debug("\tLocale: {}", locale);
  }
}

void FlatpakPlugin::process_system_installation(gpointer data,
                                                gpointer user_data) {
  auto obj = static_cast<FlatpakPlugin*>(user_data);

  GError* error = nullptr;
  auto installation = static_cast<FlatpakInstallation*>(data);

  auto install = std::make_unique<struct installation>();
  auto path = flatpak_installation_get_path(installation);
  install->id = flatpak_installation_get_id(installation);
  install->display_name = flatpak_installation_get_display_name(installation);
  install->path = g_file_get_path(path);
  install->no_interaction =
      static_cast<bool>(flatpak_installation_get_no_interaction(installation));
  install->is_user =
      static_cast<bool>(flatpak_installation_get_is_user(installation));
  install->priority = flatpak_installation_get_priority(installation);

  // Get the default languages used by the installation to decide which
  // subpaths to install of locale extensions.
  auto languages =
      flatpak_installation_get_default_languages(installation, &error);
  if (error) {
    spdlog::error(
        "[FlatpakPlugin] flatpak_installation_get_default_languages: {}",
        error->message);
    g_error_free(error);
    error = nullptr;
  }

  if (languages != nullptr) {
    std::vector<std::string> languages_;
    for (auto language = languages; *language != nullptr; ++language) {
      languages_.emplace_back(*language);
    }
    g_strfreev(languages);
    install->default_languages = std::move(languages_);
  } else {
    spdlog::error("[FlatpakPlugin] Error: No default languages found.");
  }

  // Like flatpak_installation_get_default_languages() but includes territory
  // information (e.g. en_US rather than en) which may be included in the
  // extra-languages configuration.
  auto locales = flatpak_installation_get_default_locales(installation, &error);
  if (error) {
    spdlog::error(
        "[FlatpakPlugin] flatpak_installation_get_default_languages: {}",
        error->message);
    g_error_free(error);
    error = nullptr;
  }
  if (locales != nullptr) {
    std::vector<std::string> locales_;
    for (auto locale = locales; *locale != nullptr; ++locale) {
      locales_.emplace_back(*locale);
    }
    g_strfreev(locales);
    install->default_locales = std::move(locales_);
  } else {
    spdlog::error("[FlatpakPlugin] Error: No default locales found.");
  }

  PrintInstallation(install);

  obj->installations_.push_back(std::move(install));

  (void)GetRemotes(installation, obj->default_arch_);

  (void)GetApplicationList(installation);
}

std::vector<char> decompressGzip(const std::vector<char>& compressedData,
                                 std::vector<char>& decompressedData) {
  z_stream zs;
  memset(&zs, 0, sizeof(zs));

  if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
    spdlog::error("[FlatpakPlugin] Unable to initialize zlib inflate");
    return {};
  }

  zs.next_in = (Bytef*)compressedData.data();
  zs.avail_in = static_cast<uInt>(compressedData.size());

  int zlibResult;
  auto buffer = std::make_unique<char[]>(BUFFER_SIZE);

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

// Function to make an XPath query and print the results
std::string executeXPathQuery(xmlDoc* doc, const char* xpathExpr) {
  std::string result;

  if (doc == nullptr) {
    spdlog::error("[FlatpakPlugin] Failed to parse XML: {}", xpathExpr);
  }

  // Create XPath context
  auto xpathCtx = xmlXPathNewContext(doc);
  if (!xpathCtx) {
    spdlog::error("[FlatpakPlugin] Failed to create XPath context: {}",
                  xpathExpr);
    xmlFreeDoc(doc);
    return {};
  }

  // Evaluate the XPath expression
  auto xpathObj = xmlXPathEvalExpression((const xmlChar*)xpathExpr, xpathCtx);
  if (!xpathObj) {
    spdlog::error("[FlatpakPlugin] Failed to evaluate XPath expression: {}",
                  xpathExpr);
    xmlXPathFreeContext(xpathCtx);
    xmlFreeDoc(doc);
    return {};
  }

  // Print the result nodes
  xmlNodeSet* nodes = xpathObj->nodesetval;
  if (nodes == nullptr) {
    spdlog::info("[FlatpakPlugin] No results found for the XPath query: {}",
                 xpathExpr);
    return {};
  } else {
    for (int i = 0; i < nodes->nodeNr; ++i) {
      xmlNode* node = nodes->nodeTab[i];
      if (node->type == XML_ELEMENT_NODE || node->type == XML_ATTRIBUTE_NODE) {
        xmlChar* content = xmlNodeGetContent(node);
        if (content) {
          result = reinterpret_cast<const char*>(content);
          xmlFree(content);
        }
      }
    }
  }

  // Cleanup
  xmlXPathFreeObject(xpathObj);
  xmlXPathFreeContext(xpathCtx);
  return result;
}

void parse_appstream_xml_string(const std::string& buffer) {
  spdlog::debug("[FlatpakPlugin] parsing {} byte XML doc", buffer.size());
  xmlDoc* doc = xmlReadMemory(buffer.data(), static_cast<int>(buffer.size()),
                              "noname.xml", nullptr, 0);
  if (doc == nullptr) {
    spdlog::error("[FlatpakPlugin] xmlReadMemory failure");
    return;
  }

  xmlFreeDoc(doc);
  xmlCleanupParser();
}

inipp::Ini<char> get_ini_file(const std::filesystem::path& filepath) {
  if (!exists(filepath)) {
    spdlog::error("[FlatpakPlugin] {}: file does not exist: {}", __FUNCTION__,
                  filepath.c_str());
    return {};
  }

  // open file as input file stream
  std::ifstream file(filepath, std::ios::binary);
  if (!file.is_open()) {
    spdlog::error("[FlatpakPlugin] {}: Failed to open file: {}", __FUNCTION__,
                  filepath.c_str());
    return {};
  }

  inipp::Ini<char> ini;
  ini.parse(file);
  return std::move(ini);
}

std::vector<char> read_file_to_vector(const std::filesystem::path& filepath) {
  if (!exists(filepath)) {
    spdlog::error("[FlatpakPlugin] {}: file does not exist: {}", __FUNCTION__,
                  filepath.c_str());
    return {};
  }

  // open file as input file stream
  std::ifstream file(filepath, std::ios::binary);
  if (!file.is_open()) {
    spdlog::error("[FlatpakPlugin] {}: Failed to open file: {}", __FUNCTION__,
                  filepath.c_str());
    return {};
  }

  // Get the file size
  file.seekg(0, std::ios::end);
  std::streampos fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  // Create a vector to store the data
  std::vector<char> rawFile(static_cast<unsigned long>(fileSize));

  // Read the entire file into the vector
  file.read(rawFile.data(), fileSize);
  if (!file) {
    spdlog::error("[FlatpakPlugin] {}: Error reading file: {}", __FUNCTION__,
                  filepath.c_str());
    return {};
  }

  file.close();

  return std::move(rawFile);
}

void FlatpakPlugin::parse_repo_appstream_xml(const char* appstream_xml) {
  // try un-compressed file first
  bool compressed = false;
  std::filesystem::path path = appstream_xml;
  if (!exists(path / "appstream.xml")) {
    path /= "appstream.xml.gz";
    if (!exists(path)) {
      spdlog::error("[FlatpakPlugin] file does not exist: {}", path.c_str());
      return;
    } else {
      compressed = true;
    }
  } else {
    path /= "appstream.xml";
  }

  // Create a vector to store the data
  std::vector<char> rawFile = read_file_to_vector(path);

  // this backs decompressedString
  std::vector<char> decompressedXml;

  if (compressed) {
    decompressGzip(rawFile, decompressedXml);
    if (decompressedXml.empty()) {
      spdlog::error("[FlatpakPlugin] failed to decompress: {}", path.c_str());
      return;
    }
    std::string decompressedString(decompressedXml.begin(),
                                   decompressedXml.end());
    parse_appstream_xml_string(decompressedString);
  } else {
    std::string decompressedString(rawFile.begin(), rawFile.end());
    parse_appstream_xml_string(decompressedString);
  }
}

void FlatpakPlugin::parse_desktop_file(const std::filesystem::path& filepath,
                                       struct desktop_file& desktop) {
  auto ini = get_ini_file(filepath);
  if (ini.sections.empty()) {
    return;
  }
  static constexpr char kDesktopEntrySection[] = "Desktop Entry";
  inipp::get_value(ini.sections[kDesktopEntrySection], "Name", desktop.name);
  inipp::get_value(ini.sections[kDesktopEntrySection], "Comment",
                   desktop.comment);
  inipp::get_value(ini.sections[kDesktopEntrySection], "Exec", desktop.exec);
  inipp::get_value(ini.sections[kDesktopEntrySection], "Icon", desktop.icon);
  inipp::get_value(ini.sections[kDesktopEntrySection], "Terminal",
                   desktop.terminal);
  inipp::get_value(ini.sections[kDesktopEntrySection], "Type", desktop.type);
  inipp::get_value(ini.sections[kDesktopEntrySection], "StartupNotify",
                   desktop.startupNotify);
  inipp::get_value(ini.sections[kDesktopEntrySection], "Categories",
                   desktop.categories);
  inipp::get_value(ini.sections[kDesktopEntrySection], "Keywords",
                   desktop.keywords);
  inipp::get_value(ini.sections[kDesktopEntrySection], "DBusActivatable",
                   desktop.dbus_activatable);
}

std::filesystem::path get_desktop_id_filepath(const std::string& desktop_id) {
  auto xdg_data_dirs = getenv("XDG_DATA_DIRS");
  if (!xdg_data_dirs) {
    spdlog::error("[FlatpakPlugin] XDG_DATA_DIRS is not set!");
    return {};
  }
  auto dirs = plugin_common::StringTools::split(xdg_data_dirs, ":");
  for (const auto& dir : dirs) {
    std::filesystem::path path = dir;
    path /= "applications";
    path /= desktop_id;
    if (exists(path)) {
      return std::move(path);
    }
  }
  return {};
}

void FlatpakPlugin::parse_appstream_xml(FlatpakInstalledRef* installed_ref,
                                        const char* deploy_dir,
                                        bool print_raw_xml) {
  GError* error = nullptr;
  GCancellable* cancellable = nullptr;

  cancellable = g_cancellable_new();
  auto g_bytes =
      flatpak_installed_ref_load_appdata(installed_ref, cancellable, &error);
  g_cancellable_cancel(cancellable);
  g_object_unref(cancellable);

  if (!g_bytes) {
    if (error != nullptr) {
      spdlog::error("[FlatpakPlugin] Failed loading appdata: {}",
                    error->message);
      g_clear_error(&error);
    }
    return;
  }

  gsize size;
  auto data = static_cast<const uint8_t*>(g_bytes_get_data(g_bytes, &size));
  std::vector<char> compressedData(data, data + size);
  std::vector<char> decompressedData;
  decompressGzip(compressedData, decompressedData);
  std::string decompressedString(decompressedData.begin(),
                                 decompressedData.end());
  if (print_raw_xml) {
    std::cout << decompressedString.data() << std::endl;
  }

  xmlDoc* doc = xmlReadMemory(decompressedString.data(),
                              static_cast<int>(decompressedString.size()),
                              "noname.xml", nullptr, 0);
  if (doc == nullptr) {
    spdlog::error("[FlatpakPlugin] xmlReadMemory failure");
    return;
  }

  auto origin = executeXPathQuery(doc, "//components[1]/@origin");
  auto version = executeXPathQuery(doc, "//components[1]/@version");
  auto type = executeXPathQuery(doc, "//components[1]/component[1]/@type");

  auto id = executeXPathQuery(doc, "//components[1]/component[1]/id");
  auto pkgname = executeXPathQuery(doc, "//components[1]/component[1]/pkgname");
  auto source_pkgname =
      executeXPathQuery(doc, "//components[1]/component[1]/source_pkgname");
  auto name =
      executeXPathQuery(doc, "//components[1]/component[1]/name[not(@*)]");
  auto project_license =
      executeXPathQuery(doc, "//components[1]/component[1]/project_license");
  auto summary =
      executeXPathQuery(doc, "//components[1]/component[1]/summary[not(@*)]");
  auto description = executeXPathQuery(
      doc, "//components[1]/component[1]/description[not(@*)]");

  std::string launchable;
  std::string icon;
  std::string desktop_id;
  desktop_file desktop{};

  if (type == "desktop" || type == "desktop-application") {
    std::filesystem::path icon_path = deploy_dir;
    icon_path /= kIconPathSuffix;
    icon_path /= executeXPathQuery(doc, kIconXpathQuery);
    if (!exists(icon_path)) {
      spdlog::error("[FlatpakPlugin] icon path does not exist: {}",
                    icon_path.c_str());
    }

    icon = "\n\ticon: ";
    icon.append(icon_path);

    desktop_id = executeXPathQuery(
        doc, "//components[1]/component[1]/launchable[@type='desktop-id']");

    launchable = "\n\tlaunchable: ";
    launchable.append(desktop_id);

    auto desktop_id_filepath = get_desktop_id_filepath(desktop_id);
    if (!desktop_id_filepath.empty()) {
      parse_desktop_file(desktop_id_filepath, desktop);
    }
  }

  xmlFreeDoc(doc);

  auto appdata_name = flatpak_installed_ref_get_appdata_name(installed_ref);

  spdlog::info(
      "[FlatpakPlugin] [{}] appstream XML\n\torigin: \"{}\"\n\tversion: "
      "\"{}\"\n\ttype: \"{}\"\n\tid: \"{}\"\n\tpkgname: "
      "\"{}\"\n\tsource_pkgname: "
      "\"{}\"\n\tname: \"{}\"\n\tproject_license: \"{}\"\n\tsummary: "
      "\"{}\"\n\tdescription: "
      "\"{}\"{}{}",
      appdata_name ? appdata_name : "", origin.empty() ? "" : origin,
      version.empty() ? "" : version, type.empty() ? "" : type,
      id.empty() ? "" : id, pkgname.empty() ? "" : pkgname,
      source_pkgname.empty() ? "" : source_pkgname, name.empty() ? "" : name,
      project_license.empty() ? "" : project_license,
      summary.empty() ? "" : summary, description.empty() ? "" : description,
      icon.empty() ? "" : icon, launchable.empty() ? "" : launchable);

  if (type == "desktop" || type == "desktop-application") {
    spdlog::info(
        "[FlatpakPlugin] {}\n\tname: \"{}\"\n\tcomment: \"{}\"\n\texec: "
        "{}\n\ticon: \"{}\"\n\ttype: \"{}\"\n\tstartupNotify: "
        "\"{}\"\n\tcategories: \"{}\"\n\tkeywords: \"{}\"\n\tdbus_activatable: "
        "\"{}\"",
        desktop_id, desktop.name.empty() ? "" : desktop.name,
        desktop.comment.empty() ? "" : desktop.comment,
        desktop.exec.empty() ? "" : desktop.exec,
        desktop.icon.empty() ? "" : desktop.icon, desktop.terminal,
        desktop.type.empty() ? "" : desktop.type, desktop.startupNotify,
        desktop.categories.empty() ? "" : desktop.categories,
        desktop.keywords.empty() ? "" : desktop.keywords,
        desktop.dbus_activatable.empty() ? "" : desktop.dbus_activatable);
  }

  g_bytes_unref(g_bytes);
}

std::string FlatpakPlugin::get_application_id(
    FlatpakInstalledRef* installed_ref) {
  std::string result;
  GError* error = nullptr;
  GCancellable* cancellable = nullptr;

  cancellable = g_cancellable_new();
  auto g_bytes =
      flatpak_installed_ref_load_metadata(installed_ref, cancellable, &error);
  g_cancellable_cancel(cancellable);
  g_object_unref(cancellable);

  if (!g_bytes) {
    if (error != nullptr) {
      spdlog::error("[FlatpakPlugin] Error loading metadata: %s\n",
                    error->message);
      g_clear_error(&error);
    }
    return std::move(result);
  }

  gsize size;
  auto data = static_cast<const char*>(g_bytes_get_data(g_bytes, &size));
  std::string str(data, size);
  std::stringstream ss(str);

  inipp::Ini<char> metadata;
  metadata.parse(ss);

  inipp::get_value(metadata.sections["Runtime"], "name", result);
  // If Runtime section is not present, look for Application section
  if (result.empty()) {
    inipp::get_value(metadata.sections["Application"], "name", result);
  }

  g_bytes_unref(g_bytes);
  return std::move(result);
}

#if 0   // TODO
void print_content_rating(GHashTable* content_rating) {
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init(&iter, content_rating);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    spdlog::debug("[FlatpakPlugin] content_rating: Key: {}, Value: {}",
                  static_cast<char*>(key), static_cast<char*>(value));
  }
}
#endif  // TODO

FlatpakPlugin::FlatpakPlugin()
    : io_context_(std::make_unique<asio::io_context>(ASIO_CONCURRENCY_HINT_1)),
      work_(io_context_->get_executor()),
      strand_(std::make_unique<asio::io_context::strand>(*io_context_)) {
  thread_ = std::thread([&] { io_context_->run(); });

  asio::post(*strand_, [&]() {
    pthread_self_ = pthread_self();
    spdlog::debug("[FlatpakPlugin] thread_id=0x{:x}", pthread_self_);
  });

  version_ = {
      .major = FLATPAK_MAJOR_VERSION,
      .minor = FLATPAK_MINOR_VERSION,
      .micro = FLATPAK_MICRO_VERSION,
  };

  default_arch_ = flatpak_get_default_arch();

  auto* supported_arches = flatpak_get_supported_arches();
  if (supported_arches) {
    for (auto arch = supported_arches; *arch != nullptr; ++arch) {
      supported_arches_.emplace_back(*arch);
    }
  }

  spdlog::debug("[FlatpakPlugin]");
  spdlog::debug("\tFlatpak v{}.{}.{}", version_.major, version_.minor,
                version_.micro);
  spdlog::debug("\tDefault Arch: {}", default_arch_);
  spdlog::debug("\tSupported Arches:");
  for (const auto& arch : supported_arches_) {
    spdlog::debug("\t\t{}", arch);
  }

  GetInstallations();

  //  ListSystemApps();
}

std::future<void> FlatpakPlugin::GetInstallations() {
  std::lock_guard lock(installations_mutex_);

  auto promise(std::make_shared<std::promise<void>>());
  auto future(promise->get_future());

  asio::post(*strand_, [&, promise = std::move(promise)] {
    for (auto& install : installations_) {
      install.reset();
    }

    GError* error = nullptr;
    GCancellable* cancellable = g_cancellable_new();
    auto sys_installs = flatpak_get_system_installations(cancellable, &error);
    if (error) {
      spdlog::error("[FlatpakPlugin] Error getting system installations: {}",
                    error->message);
      g_clear_error(&error);
    }
    g_cancellable_cancel(cancellable);
    g_object_unref(cancellable);

    g_ptr_array_foreach(sys_installs, process_system_installation, this);
    g_ptr_array_unref(sys_installs);

    promise->set_value();
  });

  return future;
}

FlatpakPlugin::~FlatpakPlugin() {
  for (auto& install : installations_) {
    install.reset();
  }
}

}  // namespace flatpak_plugin