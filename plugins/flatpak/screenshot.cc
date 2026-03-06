#include "screenshot.h"

#include <libxml/xmlstring.h>

#include "flatpak_shim.h"
#include "plugins/common/string/string_tools.h"
#include "tools/logging.h"

using flatpak_plugin::FlatpakShim;

Image::Image(const xmlNode* node) {
  parseXmlNode(node);
}

void Image::parseXmlNode(const xmlNode* node) {
  type_ = FlatpakShim::getAttribute(node, "type");
  if (const auto widthAttr = FlatpakShim::getOptionalAttribute(node, "width")) {
    try {
      width_ = std::stoi(*widthAttr);
    } catch (const std::invalid_argument& e) {
      spdlog::error("Invalid width attribute: {} - {}", *widthAttr, e.what());
    } catch (const std::out_of_range& e) {
      spdlog::error("Width attribute out of range: {} - {}", *widthAttr,
                    e.what());
    }
  }
  if (const auto heightAttr =
          FlatpakShim::getOptionalAttribute(node, "height")) {
    try {
      height_ = std::stoi(*heightAttr);
    } catch (const std::invalid_argument& e) {
      spdlog::error("Invalid height attribute: {} - {}", *heightAttr, e.what());
    } catch (const std::out_of_range& e) {
      spdlog::error("Height attribute out of range: {} - {}", *heightAttr,
                    e.what());
    }
  }
  if (const xmlChar* content = xmlNodeGetContent(node)) {
    url_ = std::string(reinterpret_cast<const char*>(content));
    std::string rawUrl = std::string(reinterpret_cast<const char*>(content));
    url_ = plugin_common::StringTools::trimSpaces(rawUrl);
    xmlFree(const_cast<xmlChar*>(content));
  } else {
    spdlog::error("Failed to retrieve content for node.");
  }
}

const std::optional<std::string>& Image::getType() const {
  return type_;
}

const std::optional<int>& Image::getWidth() const {
  return width_;
}

const std::optional<int>& Image::getHeight() const {
  return height_;
}

const std::optional<std::string>& Image::getUrl() const {
  return url_;
}

void Image::printImageDetails() const {
  spdlog::info("\tImage:");
  if (type_.has_value())
    spdlog::info("\t\tType: {}", type_.value());
  if (width_.has_value())
    spdlog::info("\t\tWidth: {}", width_.value());
  if (height_.has_value())
    spdlog::info("\t\tHeight: {}", height_.value());
  if (url_.has_value())
    spdlog::info("\t\tURL: {}", url_.value());
}

Video::Video(const xmlNode* node) {
  parseXmlNode(node);
}

void Video::parseXmlNode(const xmlNode* node) {
  container_ = FlatpakShim::getAttribute(node, "container");
  codec_ = FlatpakShim::getAttribute(node, "codec");
  if (const auto widthAttr = FlatpakShim::getOptionalAttribute(node, "width")) {
    try {
      width_ = std::stoi(*widthAttr);
    } catch (const std::invalid_argument& e) {
      spdlog::error("Invalid width attribute: {} - {}", *widthAttr, e.what());
    } catch (const std::out_of_range& e) {
      spdlog::error("Width attribute out of range: {} - {}", *widthAttr,
                    e.what());
    }
  }
  if (const auto heightAttr =
          FlatpakShim::getOptionalAttribute(node, "height")) {
    try {
      height_ = std::stoi(*heightAttr);
    } catch (const std::invalid_argument& e) {
      spdlog::error("Invalid height attribute: {} - {}", *heightAttr, e.what());
    } catch (const std::out_of_range& e) {
      spdlog::error("Height attribute out of range: {} - {}", *heightAttr,
                    e.what());
    }
  }
  if (const xmlChar* content = xmlNodeGetContent(node)) {
    std::string rawUrl = std::string(reinterpret_cast<const char*>(content));
    url_ = plugin_common::StringTools::trimSpaces(rawUrl);
    xmlFree(const_cast<xmlChar*>(content));
  } else {
    spdlog::error("Failed to retrieve content for node.");
  }
}

const std::optional<std::string>& Video::getContainer() const {
  return container_;
}

const std::optional<std::string>& Video::getCodec() const {
  return codec_;
}

const std::optional<int>& Video::getWidth() const {
  return width_;
}

const std::optional<int>& Video::getHeight() const {
  return height_;
}

const std::optional<std::string>& Video::getUrl() const {
  return url_;
}

void Video::printVideoDetails() const {
  spdlog::info("\tVideo:");
  if (container_.has_value())
    spdlog::info("\t\tContainer: {}", container_.value());
  if (codec_.has_value())
    spdlog::info("\t\tCodec: {}", codec_.value());
  if (width_.has_value())
    spdlog::info("\t\tWidth: {}", width_.value());
  if (height_.has_value())
    spdlog::info("\t\tHeight: {}", height_.value());
  if (url_.has_value())
    spdlog::info("\t\tURL: {}", url_.value());
}

Screenshot::Screenshot(const xmlNode* node) {
  parseXmlNode(node);
}

void Screenshot::parseXmlNode(const xmlNode* node) {
  std::vector<Image> images;
  type_ = FlatpakShim::getAttribute(node, "type");
  for (xmlNode* current = node->children; current; current = current->next) {
    // skip appstream whitespace
    if (current->type != XML_ELEMENT_NODE) {
      continue;
    }
    if (xmlStrEqual(current->name,
                    reinterpret_cast<const xmlChar*>("caption"))) {
      if (const xmlChar* content = xmlNodeGetContent(current)) {
        std::string rawCaption =
            std::string(reinterpret_cast<const char*>(content));
        captions_.emplace_back(
            plugin_common::StringTools::trimSpaces(rawCaption));
        xmlFree(const_cast<xmlChar*>(content));
      } else {
        spdlog::error("Failed to retrieve caption content.");
      }
    } else if (xmlStrEqual(current->name,
                           reinterpret_cast<const xmlChar*>("image"))) {
      images.emplace_back(current);
    } else if (xmlStrEqual(current->name,
                           reinterpret_cast<const xmlChar*>("video"))) {
      video_ = Video(current);
    }
  }

  if (!images.empty()) {
    images_ = std::move(images);
  }
}

const std::optional<std::string>& Screenshot::getType() const {
  return type_;
}

const std::vector<std::string>& Screenshot::getCaptions() const {
  return captions_;
}

const std::optional<std::vector<Image>>& Screenshot::getImages() const {
  return images_;
}

const std::optional<Video>& Screenshot::getVideo() const {
  return video_;
}

void Screenshot::printScreenshotDetails() const {
  if (type_.has_value()) {
    spdlog::info("\tScreenshot:");
    spdlog::info("\t\tType: {}", type_.value());
  } else {
    return;
  }

  for (const auto& caption : captions_) {
    spdlog::info("\t\tCaption: {}", caption);
  }

  if (images_.has_value()) {
    for (const auto& image : images_.value()) {
      image.printImageDetails();
    }
  }

  if (video_.has_value()) {
    video_->printVideoDetails();
  }
}