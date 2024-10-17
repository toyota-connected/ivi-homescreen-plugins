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
#pragma once
#ifndef FLUTTER_PLUGIN_FILAMENT_VIEW_PLUGIN_H_
#define FLUTTER_PLUGIN_FILAMENT_VIEW_PLUGIN_H_

#include <memory>

#include <core/scene/serialization/scene_text_deserializer.h>
#include <flutter/plugin_registrar.h>
#include <flutter_homescreen.h>
#include <messages.g.h>
#include <platform_views/platform_view.h>
#include <wayland/display.h>

namespace plugin_filament_view {

class FilamentViewPlugin : public flutter::Plugin,
                           public FilamentViewApi,
                           public ModelStateChannelApi,
                           public SceneStateApi,
                           public ShapeStateApi,
                           public RendererChannelApi,
                           public PlatformView {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrar* registrar,
                                    int32_t id,
                                    std::string viewType,
                                    int32_t direction,
                                    double top,
                                    double left,
                                    double width,
                                    double height,
                                    const std::vector<uint8_t>& params,
                                    const std::string& assetDirectory,
                                    FlutterDesktopEngineRef engine,
                                    PlatformViewAddListener addListener,
                                    PlatformViewRemoveListener removeListener,
                                    void* platform_view_context);

  FilamentViewPlugin(int32_t id,
                     std::string viewType,
                     int32_t direction,
                     double top,
                     double left,
                     double width,
                     double height,
                     const std::vector<uint8_t>& params,
                     const std::string& assetDirectory,
                     PlatformViewAddListener addListener,
                     PlatformViewRemoveListener removeListener,
                     void* platform_view_context);

  ~FilamentViewPlugin() override;

  void ChangeDirectLightByIndex(
      int32_t index,
      std::string color,
      int32_t intensity,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ToggleShapesInScene(
      bool value,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ToggleDebugCollidableViewsInScene(
      bool value,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeCameraMode(
      std::string szValue,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void vResetInertiaCameraToDefaultValues(
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void SetCameraRotation(
      float fValue,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeAnimationByIndex(
      int32_t index,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeAnimationByName(
      std::string name,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void GetAnimationNames(
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void GetAnimationCount(
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void GetCurrentAnimationIndex(
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void GetAnimationNameByIndex(
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeSkyboxByAsset(
      std::string path,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeSkyboxByUrl(
      std::string url,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeSkyboxByHdrAsset(
      std::string path,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeSkyboxByHdrUrl(
      std::string url,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeSkyboxColor(
      std::string color,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeToTransparentSkybox(
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeLightByKtxAsset(
      std::string path,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeLightByKtxUrl(
      std::string url,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeLightByIndirectLight(
      std::string path,
      double intensity,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeLightByHdrUrl(
      std::string path,
      double intensity,
      std::function<void(std::optional<FlutterError> reply)> result) override;

  void ChangeToDefaultIndirectLight(
      std::function<void(std::optional<FlutterError> reply)> result) override;

  // Disallow copy and assign.
  FilamentViewPlugin(const FilamentViewPlugin&) = delete;

  FilamentViewPlugin& operator=(const FilamentViewPlugin&) = delete;

 private:
  int32_t id_;
  void* platformViewsContext_;
  PlatformViewRemoveListener removeListener_;
  const std::string flutterAssetsPath_;

  static void on_resize(double width, double height, void* data);
  static void on_set_direction(int32_t direction, void* data);
  static void on_set_offset(double left, double top, void* data);
  static void on_touch(int32_t action,
                       int32_t point_count,
                       size_t point_data_size,
                       const double* point_data,
                       void* data);
  static void on_dispose(bool hybrid, void* data);

  static const struct platform_view_listener platform_view_listener_;
};

}  // namespace plugin_filament_view

#endif  // FLUTTER_PLUGIN_FILAMENT_VIEW_PLUGIN_H_
