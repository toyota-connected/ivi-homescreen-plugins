/*
 * Copyright 2020-2023 Toyota Connected North America
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

#include "file_selector_plugin.h"

#include <filesystem>
#include <memory>
#include <sstream>

#include <flutter/plugin_registrar.h>

#include "messages.g.h"
#include "plugins/common/string/string_tools.h"
#include "tools/command.h"
#include "tools/logging.h"

namespace plugin_file_selector {
// static
void FileSelectorPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrar* registrar) {
  auto plugin = std::make_unique<FileSelectorPlugin>();

  SetUp(registrar->messenger(), plugin.get());

  registrar->AddPlugin(std::move(plugin));
}

FileSelectorPlugin::FileSelectorPlugin() = default;

FileSelectorPlugin::~FileSelectorPlugin() = default;

ErrorOr<flutter::EncodableList> FileSelectorPlugin::ShowFileChooser(
    const PlatformFileChooserActionType& type,
    const PlatformFileChooserOptions& options) {
  std::ostringstream cmd;
  std::ostringstream cmd_options;

  if (options.current_folder_path()) {
    spdlog::debug("[file_selector] Current Folder Path: {}",
                  options.current_folder_path()->c_str());
    if (std::filesystem::exists(options.current_folder_path()->c_str())) {
      cmd << "cd " << options.current_folder_path()->c_str() << " && ";
    }
  } else {
    spdlog::debug("[file_selector] No Current Folder Path specified");
  }

  if (options.allowed_file_types()) {
    std::stringstream label;
    std::stringstream extensions;
    for (const auto& file_type : *options.allowed_file_types()) {
      const auto& type_group = std::any_cast<const PlatformTypeGroup&>(
          std::get<flutter::CustomEncodableValue>(file_type));
      if (label.tellp() > 0) {
        label << " + ";
      }
      label << type_group.label();
      spdlog::debug("[file_selector] Allowed File Type Group Label: {}",
                    type_group.label());
      for (const auto& extension : type_group.extensions()) {
        spdlog::debug("[file_selector]  - Extension: {}",
                      std::get<std::string>(extension));
        extensions << " " << std::get<std::string>(extension);
      }
      for (const auto& mime_type : type_group.mime_types()) {
        spdlog::debug("[file_selector]  - MIME Type: {}",
                      std::get<std::string>(mime_type));
      }
    }
    if (!label.str().empty() && !extensions.str().empty()) {
      cmd_options << " --file-filter=\"" << label.str() << " | "
                  << extensions.str() << "\" --separator='|'";
    }
  } else {
    spdlog::debug("[file_selector] No Allowed File Types specified");
  }

  if (options.current_name()) {
    cmd_options << " --filename=" << options.current_name()->c_str();
    spdlog::debug("[file_selector] Current Name: {}",
                  options.current_name()->c_str());
  } else {
    spdlog::debug("[file_selector] No Current Name specified");
  }

  if (options.accept_button_label()) {
    cmd_options << " --title=" << options.accept_button_label()->c_str();
    spdlog::debug("[file_selector] Accept Button Label: {}",
                  options.accept_button_label()->c_str());
  }

  if (options.select_multiple()) {
    cmd_options << " --multiple";
    spdlog::debug("[file_selector] Select Multiple: true");
  } else {
    spdlog::debug("[file_selector] Select Multiple: false");
  }

  switch (type) {
    case PlatformFileChooserActionType::kOpen:
      spdlog::debug("[file_selector] ShowFileChooser: Open");
      cmd << "zenity --file-selection " << cmd_options.str();
      break;
    case PlatformFileChooserActionType::kChooseDirectory:
      spdlog::debug("[file_selector] ShowFileChooser: ChooseDirectory");
      cmd << "zenity --file-selection --directory" << cmd_options.str();
      break;
    case PlatformFileChooserActionType::kSave:
      spdlog::debug("[file_selector] ShowFileChooser: Save");
      cmd << "zenity --file-selection --save" << cmd_options.str();
      break;
    default:
      return FlutterError("unsupported-operation");
  }

  spdlog::debug("[file_selector] {}", cmd.str());
  std::string path;
  if (!plugin_common::Command::Execute(cmd.str().c_str(), path)) {
    return FlutterError("failed to execute command");
  }
  spdlog::debug("[file_selector] zenity result: {}", path);
  flutter::EncodableList results;
  auto paths = plugin_common::StringTools::split(path, "|");
  for (auto p : paths) {
    results.emplace_back(plugin_common::StringTools::trim(p, "\n").c_str());
  }
  return {std::move(results)};
}
}  // namespace plugin_file_selector
