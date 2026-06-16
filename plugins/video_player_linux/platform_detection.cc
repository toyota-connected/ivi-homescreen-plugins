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

#include "platform_detection.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <plugins/common/common.h>

namespace video_player_linux {

namespace {

// Read /sys/firmware/devicetree/base/compatible. The file is a
// null-terminated list of strings packed together (multiple
// compatible entries, most specific first). Return the raw bytes
// with embedded NULs intact so our substring search matches every
// entry. Empty string on error / no DT (x86 / ACPI systems).
std::string ReadDtCompatible() {
  constexpr const char* kPath = "/sys/firmware/devicetree/base/compatible";
  std::ifstream f(kPath, std::ios::binary);
  if (!f) {
    return {};
  }
  std::string out((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
  return out;
}

// Check if `needle` appears anywhere in `haystack`. DT compatible
// strings are substring-matched because we want to catch both
// board-specific ("renesas,salvator-xs") and SoC-generic
// ("renesas,r8a77951") entries in the same null-separated blob.
bool Contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

// Map DT compatible prefixes to PlatformProfile. First match wins.
// Order matters for overlapping entries (rk3588s must precede rk3588
// so "rockchip,rk3588s" isn't shadowed by "rockchip,rk3588").
PlatformProfile PlatformFromDtCompatible(const std::string& compat) {
  if (compat.empty()) {
    return PlatformProfile::Auto;
  }

  struct Match {
    const char* compat;
    PlatformProfile profile;
  };
  static constexpr std::array<Match, 24> kTable{{
      // NXP i.MX
      {"fsl,imx8mp", PlatformProfile::Imx8MPlus},
      {"fsl,imx8mq", PlatformProfile::Imx8M},
      {"fsl,imx8mm", PlatformProfile::Imx8M},
      {"fsl,imx8mn", PlatformProfile::Imx8M},
      {"fsl,imx8qm", PlatformProfile::Imx8QM},
      {"fsl,imx8qxp", PlatformProfile::Imx8QXP},
      {"fsl,imx95", PlatformProfile::Imx95},
      // Rockchip — rk3588s must come before rk3588
      {"rockchip,rk3588s", PlatformProfile::RockchipRK3588},
      {"rockchip,rk3588", PlatformProfile::RockchipRK3588},
      {"rockchip,rk3568", PlatformProfile::RockchipRK3568},
      {"rockchip,rk3566", PlatformProfile::RockchipRK3568},
      // Renesas R-Car Gen3/Gen4
      {"renesas,r8a779g0", PlatformProfile::RcarV4H},
      {"renesas,r8a779f0", PlatformProfile::RcarH4},
      {"renesas,r8a77950", PlatformProfile::RcarH3},
      {"renesas,r8a77951", PlatformProfile::RcarH3},
      {"renesas,r8a77960", PlatformProfile::RcarM3},
      {"renesas,r8a77965", PlatformProfile::RcarM3},
      {"renesas,r8a77980", PlatformProfile::RcarV3H},
      // Qualcomm Snapdragon Auto — sa8295p before sa8295
      {"qcom,sa8295p", PlatformProfile::QualcommSA8295},
      {"qcom,sa8295", PlatformProfile::QualcommSA8295},
      {"qcom,sa8155p", PlatformProfile::QualcommSA8155},
      {"qcom,sa8155", PlatformProfile::QualcommSA8155},
      // MediaTek
      {"mediatek,mt8195", PlatformProfile::MediatekMT8195},
      {"mediatek,mt8678", PlatformProfile::MediatekCTX1},
  }};

  for (const auto& m : kTable) {
    if (Contains(compat, m.compat)) {
      return m.profile;
    }
  }
  return PlatformProfile::Auto;
}

// Vendor-specific sysfs probes for platforms with thin DTs or
// non-standard compatible strings. Best-effort — a miss falls back
// to the V4L2 generic probe.
PlatformProfile ProbeViaSysfs() {
  namespace fs = std::filesystem;
  std::error_code ec;

  // Qualcomm: /sys/devices/soc0/family or /sys/devices/soc0/machine
  // populated by qcom_socinfo.
  if (std::ifstream f("/sys/devices/soc0/family"); f) {
    std::string family;
    std::getline(f, family);
    if (family.find("Snapdragon") != std::string::npos ||
        family.find("MSM8") != std::string::npos) {
      // Coarse — without a model number we can't distinguish SA8155
      // from SA8295. Caller can override via config.
      return PlatformProfile::QualcommSA8155;
    }
  }

  // MediaTek: vcodec platform device appears as
  // /sys/devices/platform/*vcodec*/
  for (const auto& entry : fs::directory_iterator(
           "/sys/devices/platform",
           fs::directory_options::skip_permission_denied, ec)) {
    if (ec) {
      break;
    }
    const auto name = entry.path().filename().string();
    if (name.find("vcodec") != std::string::npos ||
        name.find("mtk") != std::string::npos) {
      return PlatformProfile::MediatekMT8195;
    }
  }

  return PlatformProfile::Auto;
}

// Final fallback — is there any V4L2 device advertising a video
// codec? We look for the canonical device name substring in
// /sys/class/video4linux/*/name. A generic "video_codec" / "vpu" /
// "decoder" entry is enough to light up the GenericV4L2 backend.
bool HasV4L2CodecDevice() {
  namespace fs = std::filesystem;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(
           "/sys/class/video4linux",
           fs::directory_options::skip_permission_denied, ec)) {
    if (ec) {
      return false;
    }
    const auto name_path = entry.path() / "name";
    std::ifstream f(name_path);
    if (!f) {
      continue;
    }
    std::string name;
    std::getline(f, name);
    // Lowercase substring match is good enough. Known substrings
    // (confirmed on a handful of platforms):
    //   "venus" (Qualcomm)
    //   "mtk-vcodec" (MediaTek)
    //   "amphion" (NXP i.MX 8QM)
    //   "rockchip-vpu" / "rkvdec" (Rockchip)
    //   "hantro" (i.MX 8M family)
    //   "codec" is the safe catch-all
    for (auto& c : name) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (name.find("codec") != std::string::npos ||
        name.find("vpu") != std::string::npos ||
        name.find("rkvdec") != std::string::npos ||
        name.find("venus") != std::string::npos ||
        name.find("hantro") != std::string::npos ||
        name.find("amphion") != std::string::npos ||
        name.find("decoder") != std::string::npos) {
      return true;
    }
  }
  return false;
}

// Display-name table for log output + config.toml keys. Must match
// plan.md §2.3 and the enum values in backend_interface.h.
constexpr std::pair<PlatformProfile, const char*> kProfileNames[] = {
    {PlatformProfile::Auto, "auto"},
    {PlatformProfile::Imx8M, "imx8m"},
    {PlatformProfile::Imx8MPlus, "imx8m_plus"},
    {PlatformProfile::Imx8QM, "imx8qm"},
    {PlatformProfile::Imx8QXP, "imx8qxp"},
    {PlatformProfile::Imx95, "imx95"},
    {PlatformProfile::RockchipRK3568, "rockchip_rk3568"},
    {PlatformProfile::RockchipRK3588, "rockchip_rk3588"},
    {PlatformProfile::RcarH3, "rcar_h3"},
    {PlatformProfile::RcarM3, "rcar_m3"},
    {PlatformProfile::RcarV3H, "rcar_v3h"},
    {PlatformProfile::RcarV4H, "rcar_v4h"},
    {PlatformProfile::RcarH4, "rcar_h4"},
    {PlatformProfile::QualcommSA8155, "qualcomm_sa8155"},
    {PlatformProfile::QualcommSA8295, "qualcomm_sa8295"},
    {PlatformProfile::MediatekMT8195, "mediatek_mt8195"},
    {PlatformProfile::MediatekCTX1, "mediatek_ct_x1"},
    {PlatformProfile::GenericV4L2, "generic_v4l2"},
    {PlatformProfile::Software, "software"},
};

}  // namespace

PlatformProfile DetectPlatform() {
  // 1. Device tree — the authoritative source on ARM SoCs.
  const std::string compat = ReadDtCompatible();
  if (auto p = PlatformFromDtCompatible(compat); p != PlatformProfile::Auto) {
    SPDLOG_DEBUG("[VideoPlayer] Platform from devicetree: {}",
                 PlatformProfileName(p));
    return p;
  }

  // 2. Vendor sysfs probes for systems with thin DTs.
  if (auto p = ProbeViaSysfs(); p != PlatformProfile::Auto) {
    SPDLOG_DEBUG("[VideoPlayer] Platform from sysfs: {}",
                 PlatformProfileName(p));
    return p;
  }

  // 3. Any V4L2 codec device → generic hardware baseline.
  if (HasV4L2CodecDevice()) {
    SPDLOG_DEBUG("[VideoPlayer] Platform: generic_v4l2 (found V4L2 codec)");
    return PlatformProfile::GenericV4L2;
  }

  // 4. Nothing found → software fallback.
  SPDLOG_DEBUG("[VideoPlayer] Platform: software (no hardware decoder found)");
  return PlatformProfile::Software;
}

std::string PlatformProfileName(PlatformProfile p) {
  for (const auto& [profile, name] : kProfileNames) {
    if (profile == p) {
      return name;
    }
  }
  return "unknown";
}

PlatformProfile PlatformProfileFromName(std::string_view name) {
  for (const auto& [profile, pname] : kProfileNames) {
    if (name == pname) {
      return profile;
    }
  }
  return PlatformProfile::Auto;
}

}  // namespace video_player_linux
