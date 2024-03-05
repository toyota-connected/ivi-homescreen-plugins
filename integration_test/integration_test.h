/*
 * Copyright 2023 Toyota Connected North America
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

#include <shell/platform/embedder/embedder.h>

class IntegrationTestPlugin {
 public:
  static constexpr char kChannelName[] = "plugins.flutter.io/integration_test";

  static constexpr char kCaptureScreenshot[] = "captureScreenshot";
  static constexpr char kConvertFlutterSurfaceToImage[] =
      "convertFlutterSurfaceToImage";
  static constexpr char kRevertFlutterImage[] = "revertFlutterImage";
  static constexpr char kScheduleFrame[] = "scheduleFrame";
  static constexpr char kAllTestsFinished[] = "allTestsFinished";
  static constexpr char kArgResults[] = "results";

  static constexpr char kResultSuccess[] = "success";

  /**
   * @brief Callback function for platform messages about Integration Test
   * @param[in] message Received message
   * @param[in] userdata Pointer to User data
   * @return void
   * @relation
   * flutter
   *
   * Used for handling method calls from Dart related to
   * Integration Test
   */
  static void OnPlatformMessage(const FlutterPlatformMessage* message,
                                void* userdata);
};
