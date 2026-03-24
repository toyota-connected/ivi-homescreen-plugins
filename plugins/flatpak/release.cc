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

#include "release.h"

#include <iomanip>
#include <sstream>

#include "flatpak_shim.h"

using flatpak_plugin::FlatpakShim;

Release::Release(xmlNode* node) {
  for (xmlNode* current = node->children; current; current = current->next) {
    if (current->type == XML_ELEMENT_NODE) {
      std::string nodeName = reinterpret_cast<const char*>(current->name);
      const std::string content =
          reinterpret_cast<const char*>(xmlNodeGetContent(current));

      if (nodeName == "release") {
        version_ = FlatpakShim::getAttribute(current, "version");
        auto timestamp = FlatpakShim::getAttribute(current, "timestamp");
        // Convert timestamp to ISO 8601 format
        std::time_t time = std::stoi(timestamp);
        const std::tm* tm = std::gmtime(&time);
        std::ostringstream oss;
        oss << std::put_time(tm, "%FT%TZ");
        timestamp_ = oss.str();
      } else if (nodeName == "description") {
        description_ = content;
      } else if (nodeName == "size") {
        downloadSize_ = content;
      }
    }
  }
}

const std::string& Release::getVersion() const {
  return version_;
}

const std::string& Release::getTimestamp() const {
  return timestamp_;
}

const std::optional<std::string>& Release::getDescription() const {
  return description_;
}

const std::optional<std::string>& Release::getSize() const {
  return downloadSize_;
}
