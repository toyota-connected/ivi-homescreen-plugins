#include "commonrenderable.h"
#include "core/include/literals.h"
#include "core/utils/deserialize.h"
#include "plugins/common/common.h"

namespace plugin_filament_view {

CommonRenderable::CommonRenderable(const flutter::EncodableMap& params)
    : m_bCullingOfObjectEnabled(true),
      m_bCastShadows(false),
      m_bReceiveShadows(false) {
  Deserialize::DecodeParameterWithDefault(
      kCullingEnabled, &m_bCullingOfObjectEnabled, params, true);
  Deserialize::DecodeParameterWithDefault(kReceiveShadows, &m_bReceiveShadows,
                                          params, false);
  Deserialize::DecodeParameterWithDefault(kCastShadows, &m_bCastShadows, params,
                                          false);
}

void CommonRenderable::DebugPrint() const {
  spdlog::debug("Culling Enabled: {}", m_bCullingOfObjectEnabled);
  spdlog::debug("Receive Shadows: {}", m_bReceiveShadows);
  spdlog::debug("Cast Shadows: {}", m_bCastShadows);
}

}  // namespace plugin_filament_view