
#pragma once

#include <future>

#include <core/include/resource.h>
#include <filament/Material.h>
#include <asio/io_context_strand.hpp>

namespace plugin_filament_view {

class MaterialLoader {
 public:
  MaterialLoader();
  ~MaterialLoader() = default;

  static Resource<::filament::Material*> loadMaterialFromAsset(
      const std::string& path);

  static Resource<::filament::Material*> loadMaterialFromUrl(
      const std::string& url);

  // Disallow copy and assign.
  MaterialLoader(const MaterialLoader&) = delete;
  MaterialLoader& operator=(const MaterialLoader&) = delete;

  static void PrintMaterialInformation(const ::filament::Material* material);

 private:
};
}  // namespace plugin_filament_view