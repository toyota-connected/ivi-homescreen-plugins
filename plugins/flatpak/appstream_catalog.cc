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

#include "appstream_catalog.h"

#include <algorithm>
#include <fstream>

#include <libxml/parser.h>
#include <libxml/xmlstring.h>
#include <zlib.h>

#include "tools/logging.h"

AppstreamCatalog::AppstreamCatalog(const std::string& filePath,
                                   std::string language)
    : language_(std::move(language)) {
  parseXmlFile(filePath);
}

AppstreamCatalog::~AppstreamCatalog() = default;

void AppstreamCatalog::parseXmlFile(const std::string& filePath) {
  xmlDoc* document = xmlReadFile(filePath.c_str(), nullptr, 0);
  if (document == nullptr) {
    spdlog::error("Failed to parse {}", filePath);
    return;
  }

  const xmlNode* root = xmlDocGetRootElement(document);
  for (const xmlNode* node = root->children; node; node = node->next) {
    if (node->type == XML_ELEMENT_NODE &&
        xmlStrEqual(node->name,
                    reinterpret_cast<const xmlChar*>("component"))) {
      Component component(node, language_);
      components_.push_back(component);

      if (component.getCategories()) {
        const auto& componentCategories = component.getCategories().value();
        uniqueCategories_.insert(componentCategories.begin(),
                                 componentCategories.end());
      }
      if (component.getKeywords()) {
        const auto& componentKeywords = component.getKeywords().value();
        uniqueKeywords_.insert(componentKeywords.begin(),
                               componentKeywords.end());
      }
    }
  }

  xmlFreeDoc(document);
  xmlCleanupParser();
}

void AppstreamCatalog::decompressGzFile(const std::string& gzPath,
                                        const std::string& xmlPath) {
  const auto gz = gzopen(gzPath.c_str(), "rb");
  if (!gz) {
    spdlog::error("Failed to open {} for reading", gzPath);
    return;
  }

  std::ofstream outFile(xmlPath, std::ios::binary);
  if (!outFile) {
    spdlog::error("Failed to open {} for writing", xmlPath);
    gzclose(gz);
    return;
  }

  char buffer[8192];
  int bytesRead;
  while ((bytesRead = gzread(gz, buffer, sizeof(buffer))) > 0) {
    outFile.write(buffer, bytesRead);
  }

  outFile.close();
  gzclose(gz);
}

std::vector<Component> AppstreamCatalog::searchByCategory(
    const std::string& category,
    const bool sorted,
    const std::string& key) const {
  std::vector<Component> results;
  for (const auto& component : components_) {
    if (component.getCategories() &&
        component.getCategories().value().count(category)) {
      results.push_back(component);
    }
  }
  if (sorted) {
    std::sort(results.begin(), results.end(),
              [&key](const Component& a, const Component& b) {
                if (key == "name") {
                  return a.getName() < b.getName();
                }
                return false;
              });
  }
  return results;
}

std::vector<Component> AppstreamCatalog::searchByKeyword(
    const std::string& keyword,
    const bool sorted,
    const std::string& key) const {
  std::vector<Component> results;
  for (const auto& component : components_) {
    if (component.getKeywords() &&
        component.getKeywords().value().count(keyword)) {
      results.push_back(component);
    }
  }
  if (sorted) {
    std::sort(results.begin(), results.end(),
              [&key](const Component& a, const Component& b) {
                if (key == "name") {
                  return a.getName() < b.getName();
                }
                return false;
              });
  }
  return results;
}

std::optional<Component> AppstreamCatalog::searchById(
    const std::string& id) const {
  for (const auto& component : components_) {
    if (component.getId() == id) {
      return component;
    }
  }
  return std::nullopt;
}

size_t AppstreamCatalog::getTotalComponentCount() const {
  return components_.size();
}

std::unordered_set<std::string> AppstreamCatalog::getUniqueCategories() const {
  return uniqueCategories_;
}

std::unordered_set<std::string> AppstreamCatalog::getUniqueKeywords() const {
  return uniqueKeywords_;
}

const std::vector<Component>& AppstreamCatalog::getComponents() const {
  return components_;
}
