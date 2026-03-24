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

#include "component.h"

#include <map>

#include <libxml/tree.h>
#include <libxml/xmlstring.h>

#include "common/string/string_tools.h"
#include "flatpak_shim.h"
#include "tools/logging.h"

using flatpak_plugin::FlatpakShim;

Component::Component(const xmlNode* node, std::string language)
    : language_(std::move(language)) {
  for (xmlNode* current = node->children; current; current = current->next) {
    if (current->type == XML_ELEMENT_NODE) {
      std::string nodeName = reinterpret_cast<const char*>(current->name);
      std::string content =
          reinterpret_cast<const char*>(xmlNodeGetContent(current));

      // Required fields
      if (nodeName == "id") {
        id_ = content;
      } else if (nodeName == "name") {
        name_ = content;
      } else if (nodeName == "summary") {
        summary_ = content;
      } else if (nodeName == "pkgname") {
        pkgname_ = content;  // TODO should be a list

        // Optional fields
      } else if (nodeName == "version") {
        version_ = content;
      } else if (nodeName == "origin") {
        origin_ = content;
      } else if (nodeName == "media_baseurl") {
        mediaBaseurl_ = content;
      } else if (nodeName == "architecture") {
        architecture_ = content;
      } else if (nodeName == "project_license") {
        projectLicense_ = content;
      } else if (nodeName == "description") {
        description_ = content;
      } else if (nodeName == "url") {
        url_ = content;
      } else if (nodeName == "project_group") {
        projectGroup_ = content;
      } else if (nodeName == "icon") {
        parseIcons(current);
      } else if (nodeName == "categories") {
        parseCategories(current);
      } else if (nodeName == "keywords") {
        parseKeywords(current);
      } else if (nodeName == "screenshots" || nodeName == "screenshot") {
        parseScreenshots(current);
      } else if (nodeName == "releases") {
        parseReleases(current);
      } else if (nodeName == "launchable") {
        if (!launchable_) {
          launchable_ = std::unordered_set<std::string>{};
        }
        launchable_->insert(content);
      } else if (nodeName == "languages") {
        if (!languages_) {
          languages_ = std::unordered_set<std::string>{};
        }

        // languages in XML have whitespaces
        auto rawLangs = std::string(content);
        std::stringstream ss(rawLangs);
        std::string token;
        while (ss >> token) {
          languages_->insert(token);
        }
      } else if (nodeName == "suggests") {
        if (!suggests_) {
          suggests_ = std::unordered_set<std::string>{};
        }
        suggests_->insert(content);
      } else if (nodeName == "provides") {
        if (!provides_) {
          provides_ = std::unordered_set<std::string>{};
        }
        provides_->insert(content);
      } else if (nodeName == "compulsory_for_desktop") {
        if (!compulsoryForDesktop_) {
          compulsoryForDesktop_ = std::unordered_set<std::string>{};
        }
        compulsoryForDesktop_->insert(content);
      } else if (nodeName == "developer") {
        if (!developer_) {
          developer_ = std::unordered_set<std::string>{};
        }
        developer_->insert(content);

        // Additional Optional fields
      } else if (nodeName == "source_pkgname") {
        sourcePkgname_ = content;
      } else if (nodeName == "bundle") {
        bundle_ = content;
      } else if (nodeName == "content_rating") {
        parseContentRating(current);
      } else if (nodeName == "agreement") {
        agreement_ = content;
      } else {
        spdlog::warn("Unhandled: {}", nodeName);
      }
    }
  }
}

void Component::parseIcons(xmlNode* node) {
  if (!icons_) {
    icons_ = std::vector<Icon>{};
  }
  icons_->emplace_back(node);
}

void Component::parseCategories(const xmlNode* node) {
  if (!categories_) {
    categories_ = std::unordered_set<std::string>{};
  }
  std::vector<std::string> result = plugin_common::StringTools::split(
      reinterpret_cast<const char*>(xmlNodeGetContent(node)), "\n");
  for (auto& cat : result) {
    auto val = plugin_common::StringTools::trim(cat, " ");
    if (val.empty()) {
      continue;
    }
    categories_->insert(std::move(val));
  }
}

void Component::parseKeywords(const xmlNode* node) {
  if (!keywords_) {
    keywords_ = std::unordered_set<std::string>{};
  }

  for (xmlNodePtr current = node->children; current; current = current->next) {
    if (current->type == XML_ELEMENT_NODE &&
        xmlStrcmp(current->name, reinterpret_cast<const xmlChar*>("keyword")) ==
            0) {
      xmlChar* langAttr =
          xmlGetProp(current, reinterpret_cast<const xmlChar*>("xml:lang"));
      if (language_.empty() && langAttr == nullptr) {
        if (xmlChar* keywordContent = xmlNodeGetContent(current)) {
          keywords_->insert(reinterpret_cast<const char*>(keywordContent));
          xmlFree(keywordContent);
        } else {
          spdlog::warn("Empty <keyword> element found");
        }
      } else if (langAttr &&
                 xmlStrcmp(langAttr, reinterpret_cast<const xmlChar*>(
                                         language_.c_str())) == 0) {
        if (xmlChar* keywordContent = xmlNodeGetContent(current)) {
          keywords_->insert(reinterpret_cast<const char*>(keywordContent));
          xmlFree(keywordContent);
        } else {
          spdlog::warn("Empty <keyword> element found");
        }
      }
      xmlFree(langAttr);
    }
  }

  std::vector<std::string> result = plugin_common::StringTools::split(
      reinterpret_cast<const char*>(xmlNodeGetContent(node)), "\n");
  for (auto& cat : result) {
    auto val = plugin_common::StringTools::trim(cat, " ");
    if (val.empty()) {
      continue;
    }
    keywords_->insert(std::move(val));
  }
}

void Component::parseScreenshots(xmlNode* node) {
  if (!screenshots_) {
    screenshots_ = std::vector<Screenshot>{};
  }
  for (xmlNode* current = node->children; current; current = current->next) {
    if (current->type == XML_ELEMENT_NODE &&
        xmlStrEqual(current->name,
                    reinterpret_cast<const xmlChar*>("screenshot"))) {
      screenshots_->emplace_back(current);
    }
  }
}

void Component::parseReleases(xmlNode* node) {
  if (!releases_) {
    releases_ = std::vector<Release>{};
  }
  releases_->emplace_back(node);
}

void Component::parseContentRating(const xmlNode* node) {
  if (!contentRating_) {
    contentRating_ = std::map<std::string, ContentRatingValue>{};
  }

  if (xmlChar* type =
          xmlGetProp(node, reinterpret_cast<const xmlChar*>("type"));
      type != nullptr) {
    contentRatingType_ = reinterpret_cast<const char*>(type);
    xmlFree(type);
  }

  for (xmlNode* current = node->children; current; current = current->next) {
    if (current->type == XML_ELEMENT_NODE) {
      if (std::string nodeName = reinterpret_cast<const char*>(current->name);
          nodeName == "content_attribute") {
        auto key = FlatpakShim::getAttribute(current, "id");
        const auto value = CharToRatingValue(
            reinterpret_cast<const char*>(xmlNodeGetContent(current)));
        if (value != none) {
          contentRating_.value()[key] = value;
        }
      }
    }
  }
}

// Required fields
const std::string& Component::getId() const {
  return id_;
}

const std::string& Component::getName() const {
  return name_;
}

const std::string& Component::getSummary() const {
  return summary_;
}

const std::string& Component::getPkgName() const {
  return pkgname_;
}

// Optional fields with checks
const std::optional<std::string>& Component::getVersion() const {
  return version_;
}

const std::optional<std::string>& Component::getOrigin() const {
  return origin_;
}

const std::optional<std::string>& Component::getMediaBaseurl() const {
  return mediaBaseurl_;
}

const std::optional<std::string>& Component::getArchitecture() const {
  return architecture_;
}

const std::optional<std::string>& Component::getProjectLicense() const {
  return projectLicense_;
}

const std::optional<std::string>& Component::getDescription() const {
  return description_;
}

const std::optional<std::string>& Component::getUrl() const {
  return url_;
}

const std::optional<std::string>& Component::getProjectGroup() const {
  return projectGroup_;
}

const std::optional<std::vector<Icon>>& Component::getIcons() const {
  return icons_;
}

const std::optional<std::unordered_set<std::string>>& Component::getCategories()
    const {
  return categories_;
}

const std::optional<std::unordered_set<std::string>>& Component::getKeywords()
    const {
  return keywords_;
}

const std::optional<std::vector<Screenshot>>& Component::getScreenshots()
    const {
  return screenshots_;
}

//
const std::optional<std::unordered_set<std::string>>& Component::getLanguages()
    const {
  return languages_;
}

const std::optional<std::unordered_set<std::string>>& Component::getSuggests()
    const {
  return suggests_;
}

const std::optional<std::unordered_set<std::string>>& Component::getProvides()
    const {
  return provides_;
}

const std::optional<std::unordered_set<std::string>>&
Component::getCompulsoryForDesktop() const {
  return compulsoryForDesktop_;
}

const std::optional<std::unordered_set<std::string>>& Component::getDeveloper()
    const {
  return developer_;
}

const std::optional<std::unordered_set<std::string>>& Component::getLaunchable()
    const {
  return launchable_;
}

const std::optional<std::vector<Release>>& Component::getReleases() const {
  return releases_;
}

// Additional Optional fields
const std::optional<std::string>& Component::getSourcePkgname() const {
  return sourcePkgname_;
}

const std::optional<std::string>& Component::getBundle() const {
  return bundle_;
}

const std::optional<std::string>& Component::getContentRatingType() const {
  return contentRatingType_;
}

const std::optional<std::map<std::string, Component::ContentRatingValue>>&
Component::getContentRating() const {
  return contentRating_;
}

const std::optional<std::string>& Component::getAgreement() const {
  return agreement_;
}

std::string Component::RatingValueToString(const ContentRatingValue value) {
  switch (value) {
    case none:
      return "none";
    case mild:
      return "mild";
    case moderate:
      return "moderate";
    case intense:
      return "intense";
  }
  return "unknown";
}

Component::ContentRatingValue Component::CharToRatingValue(char const* value) {
  if (value == nullptr)
    return none;

  const std::string val(value);

  if (val == "none") {
    return none;
  }
  if (val == "mild") {
    return mild;
  }
  if (val == "moderate") {
    return moderate;
  }
  if (val == "intense") {
    return intense;
  }

  return none;
}
