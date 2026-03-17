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

#include "generated_plugin_registrant.h"

#include "config/plugins.h"
#include "tools/encodable.h"
#include "tools/logging.h"

void PluginsApiRegisterPlugins(FlutterDesktopEngineRef engine) {
  (void)engine;
#if ENABLE_PLUGIN_AUDIOPLAYERS_LINUX
  AudioPlayersLinuxPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_SECURE_STORAGE
  SecureStoragePluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_FILE_SELECTOR
  FileSelectorPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_URL_LAUNCHER
  UrlLauncherPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_GO_ROUTER
  GoRouterPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_DESKTOP_WINDOW_LINUX
  DesktopWindowLinuxPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_GOOGLE_SIGN_IN
  GoogleSignInPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_FIREBASE_CORE
  FirebaseCorePluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_FIREBASE_STORAGE
  FirebaseStoragePluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_FIREBASE_AUTH
  FirebaseAuthPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_CLOUD_FIRESTORE
  CloudFirestorePluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_VIDEO_PLAYER_LINUX
  VideoPlayerLinuxPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_CAMERA
  CameraPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_CAMERA_PIPEWIRE
  CameraPipewirePluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_PDF
  PrintingPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_RIVE_TEXT
  RiveTextPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_WEBVIEW_FLUTTER_VIEW
  WebviewFlutterPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_FLATPAK
  FlatpakPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_WEBRTC
  WebrtcPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_LAYER_PLAYGROUND_VIEW
  LayerPlaygroundPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
#if ENABLE_PLUGIN_NAV_RENDER_VIEW
  NavRenderViewPluginCApiRegisterWithRegistrar(
      FlutterDesktopGetPluginRegistrar(engine, ""));
#endif
}
