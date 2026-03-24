/*
 * Copyright 2024 Toyota Connected North America
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

#include "icon.h"

#include "flatpak_shim.h"
#include "tools/logging.h"

using flatpak_plugin::FlatpakShim;

Icon::Icon(const xmlNode* node) {
  parseXmlNode(node);
}

void Icon::parseXmlNode(const xmlNode* node) {
  type_ = FlatpakShim::getAttribute(node, "type");
  if (const auto widthAttr = FlatpakShim::getOptionalAttribute(node, "width")) {
    width_ = std::stoi(*widthAttr);
  }
  if (const auto heightAttr =
          FlatpakShim::getOptionalAttribute(node, "height")) {
    height_ = std::stoi(*heightAttr);
  }
  if (const auto scaleAttr = FlatpakShim::getOptionalAttribute(node, "scale")) {
    scale_ = std::stoi(*scaleAttr);
  }
  path_ = std::string(reinterpret_cast<const char*>(xmlNodeGetContent(node)));
}

const std::optional<std::string>& Icon::getType() const {
  return type_;
}

const std::optional<int>& Icon::getWidth() const {
  return width_;
}

const std::optional<int>& Icon::getHeight() const {
  return height_;
}

const std::optional<int>& Icon::getScale() const {
  return scale_;
}

const std::optional<std::string>& Icon::getPath() const {
  return path_;
}

void Icon::printIconDetails() const {
  if (type_.has_value()) {
    spdlog::info("\tIcon:");
    spdlog::info("\t\tType: {}", type_.value());
  }
  if (width_.has_value()) {
    spdlog::info("\t\tWidth: {}", width_.value());
  }
  if (height_.has_value()) {
    spdlog::info("\t\tHeight: {}", height_.value());
  }
  if (scale_.has_value()) {
    spdlog::info("\t\tScale: {}", scale_.value());
  }
  if (path_.has_value()) {
    spdlog::info("\t\tPath: {}", path_.value());
  }
}
