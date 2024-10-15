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

#include "webview_flutter_view_plugin.h"

#include "messages.g.h"

#include <flutter/plugin_registrar.h>

#include <memory>

#include "plugins/common/common.h"

#include "wrapper/cef_library_loader.h"

namespace plugin_webview_flutter {

// static
void WebviewFlutterPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrar* registrar) {
  auto plugin = std::make_unique<WebviewFlutterPlugin>();

  InstanceManagerHostApi::SetUp(registrar->messenger(), plugin.get());
  WebStorageHostApi::SetUp(registrar->messenger(), plugin.get());
  WebViewHostApi::SetUp(registrar->messenger(), plugin.get());
  WebSettingsHostApi::SetUp(registrar->messenger(), plugin.get());
  WebChromeClientHostApi::SetUp(registrar->messenger(), plugin.get());
  WebViewClientHostApi::SetUp(registrar->messenger(), plugin.get());
  DownloadListenerHostApi::SetUp(registrar->messenger(), plugin.get());
  JavaScriptChannelHostApi::SetUp(registrar->messenger(), plugin.get());
  CookieManagerHostApi::SetUp(registrar->messenger(), plugin.get());

  registrar->AddPlugin(std::move(plugin));
}

RenderHandler::RenderHandler() = default;

RenderHandler::~RenderHandler() = default;

void RenderHandler::GetViewRect(CefRefPtr<CefBrowser> /* browser */,
                                CefRect&  rect ) {
  spdlog::debug("[webivew_flutter] GetViewRect");  
  rect.width = 800;
  rect.height = 600;
}

void RenderHandler::OnPaint(CefRefPtr<CefBrowser> /* browser */,
                            PaintElementType /* type */,
                            const RectList& /* dirtyRects */,
                            const void* /* buffer */,
                            int width,
                            int height) {
  spdlog::debug("[webivew_flutter] OnPaint, width: {}, height: {}", width,
               height);
}

void RenderHandler::OnAcceleratedPaint(
    CefRefPtr<CefBrowser> /* browser */,
    PaintElementType /* type */,
    const RectList& /* dirtyRects */,
    const CefAcceleratedPaintInfo& /* info */) {
  spdlog::debug("[webivew_flutter] OnAcceleratedPaint");
}

WebviewFlutterPlugin::WebviewFlutterPlugin() {
  std::vector<char*> args;
  args.reserve(5);
  args.push_back("homescreen");
  // args.push_back("--no-sandbox");
  args.push_back("--use-views");
  args.push_back("--ozone-platform=wayland");
  args.push_back("--log-level=0");
  // args.push_back("--enable-logging=stderr");
  args.push_back("--v=1");

  std::string libcef_path_str = std::string(CEF_ROOT) + "/Release/libcef.so";
  std::filesystem::path libcef_file_path(libcef_path_str);
  spdlog::debug("[webview_flutter] cef_load_library");
  int cef_load_ok = cef_load_library(libcef_path_str.c_str());
  if(!cef_load_ok)
  {
    exit(-1);
  }
  spdlog::debug("[webview_flutter] cef_load_library OK!");

  
  cef_main_args_t main_args = {static_cast<int>(args.size()), args.data()};

  // Specify CEF global settings here.
  _cef_settings_t settings = {0};

  settings.no_sandbox = false;
  settings.windowless_rendering_enabled = true;
  settings.log_severity = LOGSEVERITY_VERBOSE;
  //settings.command_line_args_disabled = true;

  std::string root_cache_path_str = std::string(CEF_ROOT) + "/.config/cef_user_data";
  const char* root_cache_path = root_cache_path_str.c_str();
  cef_string_ascii_to_utf16(root_cache_path, strlen(root_cache_path), &settings.root_cache_path);

  std::string resource_path_str = std::string(CEF_ROOT) + "/Resources";
  const char* resource_path = resource_path_str.c_str();
  cef_string_ascii_to_utf16(resource_path, strlen(resource_path), &settings.resources_dir_path);

  const char* browser_subprocess_path = "/usr/local/bin/webview_flutter_subprocess";
  cef_string_ascii_to_utf16(browser_subprocess_path, strlen(browser_subprocess_path), &settings.browser_subprocess_path);

  settings.size = sizeof(_cef_settings_t);

  spdlog::debug("[webview_flutter] ++CefInitialize");
  if (!cef_initialize(&main_args, &settings, nullptr, nullptr)) {
    int error_code;
    error_code = cef_get_exit_code();
    spdlog::error("[webview_flutter] CefInitialize: {}", error_code);
    exit(EXIT_FAILURE);
  }
  spdlog::debug("[webview_flutter] --CefInitialize");


  spdlog::debug("[webview_flutter] Create and Configure Window");
  cef_window_info_t window_info;
  window_info.windowless_rendering_enabled = 0; 
  const cef_window_info_t window_const = window_info;

  spdlog::debug("[webview_flutter] Create renderHandler");
  renderHandler_ = std::make_unique<_cef_render_handler_t>();
  spdlog::debug("[webview_flutter] Create browserClient");
  // browserClient_ = std::make_unique<_cef_client_t>();

  spdlog::debug("[webview_flutter] Set browser settings");
  _cef_browser_settings_t browserSettings;
  browserSettings.windowless_frame_rate = 60;  // 30 is default

  cef_string_t browser_url_cef_str = {0};
  const char* browser_url = "https://deanm.github.io/pre3d/monster.html";
  spdlog::debug("[webview_flutter] Build url string");
  cef_string_ascii_to_utf16(browser_url, strlen(browser_url), &browser_url_cef_str);

  spdlog::debug("[webview_flutter] CreateBrowserSync++");
  browser_ = cef_browser_host_create_browser_sync(
      &window_info, browserClient_,
      &settings.browser_subprocess_path, &browserSettings, nullptr,
      nullptr);
  spdlog::debug("[webview_flutter] CreateBrowserSync--");
}

void WebviewFlutterPlugin::PlatformViewCreate(
    int32_t id,
    std::string viewType,
    int32_t direction,
    double top,
    double left,
    double width,
    double height,
    const std::vector<uint8_t>& params,
    std::string assetDirectory,
    FlutterDesktopEngineRef engine,
    PlatformViewAddListener addListener,
    PlatformViewRemoveListener removeListener,
    void* platform_view_context) {
  auto plugin = std::make_unique<WebviewPlatformView>(
      id, std::move(viewType), direction, top, left, width, height, params,
      std::move(assetDirectory), engine, addListener, removeListener,
      platform_view_context);
}

WebviewPlatformView::WebviewPlatformView(
    const int32_t id,
    std::string viewType,
    const int32_t direction,
    const double top,
    const double left,
    const double width,
    const double height,
    const std::vector<uint8_t>& /* params */,
    std::string assetDirectory,
    FlutterDesktopEngineState* state,
    const PlatformViewAddListener addListener,
    const PlatformViewRemoveListener removeListener,
    void* platform_view_context)
    : PlatformView(id,
                   std::move(viewType),
                   direction,
                   top,
                   left,
                   width,
                   height),
      id_(id),
      platformViewsContext_(platform_view_context),
      removeListener_(removeListener),
      flutterAssetsPath_(std::move(assetDirectory)),
      callback_(nullptr) {
  SPDLOG_TRACE("++WebviewFlutterPlugin::WebviewFlutterPlugin");

  /* Setup Wayland subsurface */
  const auto flutter_view = state->view_controller->view;
  display_ = flutter_view->GetDisplay()->GetDisplay();
  parent_surface_ = flutter_view->GetWindow()->GetBaseSurface();
  surface_ =
      wl_compositor_create_surface(flutter_view->GetDisplay()->GetCompositor());
  subsurface_ = wl_subcompositor_get_subsurface(
      flutter_view->GetDisplay()->GetSubCompositor(), surface_,
      parent_surface_);

  // wl_subsurface_set_sync(subsurface_);
  wl_subsurface_set_desync(subsurface_);
  wl_subsurface_set_position(subsurface_, static_cast<int32_t>(top),
                             static_cast<int32_t>(left));
  // wl_subsurface_place_above(subsurface_, parent_surface_);
  wl_subsurface_place_below(subsurface_, surface_);
  wl_surface_commit(parent_surface_);

  addListener(platformViewsContext_, id, &platform_view_listener_, this);
  SPDLOG_TRACE("--WebviewFlutterPlugin::WebviewFlutterPlugin");
}

WebviewFlutterPlugin::~WebviewFlutterPlugin() {
  browser_ = nullptr;
  browserClient_ = nullptr;
  cef_shutdown();

  renderHandler_.reset();
};

std::optional<FlutterError> WebviewFlutterPlugin::Clear() {
  spdlog::debug("[webview_flutter] Clear");
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::Create(int64_t instance_id) {
  spdlog::debug("[webview_flutter] Create, instance_id: {}", instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::DeleteAllData(
    int64_t instance_id) {
  spdlog::debug("[webview_flutter] DeleteAllData, instance_id: {}", instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::LoadData(
    int64_t instance_id,
    const std::string& /* data */,
    const std::string* mime_type,
    const std::string* encoding) {
  spdlog::debug(
      "[webview_flutter] LoadData, instance_id: {}, mime_type: {}, encoding: "
      "{}",
      instance_id, *mime_type, *encoding);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::LoadDataWithBaseUrl(
    int64_t instance_id,
    const std::string* base_url,
    const std::string& /* data */,
    const std::string* mime_type,
    const std::string* encoding,
    const std::string* history_url) {
  spdlog::debug(
      "[webview_flutter] LoadDataWithBaseUrl, instance_id: {}, base_url: {}, "
      "mime_type: "
      "{}, encoding: {}, history_url: {}",
      instance_id, *base_url, *mime_type, *encoding, *history_url);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::LoadUrl(
    int64_t instance_id,
    const std::string& url,
    const flutter::EncodableMap& headers) {
  spdlog::debug("[webview_flutter] LoadUrl, instance_id: {}, url: {}",
               instance_id, url);
  if (!headers.empty()) {
    plugin_common::Encodable::PrintFlutterEncodableMap("headers", headers);
  }
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::PostUrl(
    int64_t instance_id,
    const std::string& url,
    const std::vector<uint8_t>& /* data */) {
  spdlog::debug("[webview_flutter] PostUrl: instance_id: {}, url: {}",
               instance_id, url);
  return std::nullopt;
}

ErrorOr<std::optional<std::string>> WebviewFlutterPlugin::GetUrl(
    int64_t instance_id) {
  spdlog::debug("[webview_flutter] GetUrl, instance_id: {}", instance_id);
  // TODO - set favorite in test case calls this
  return {"https://www.google.com"};
}

ErrorOr<bool> WebviewFlutterPlugin::CanGoBack(int64_t instance_id) {
  spdlog::debug("[webview_flutter] CanGoBack, instance_id: {}", instance_id);
  return {true};
}

ErrorOr<bool> WebviewFlutterPlugin::CanGoForward(int64_t instance_id) {
  spdlog::debug("[webview_flutter] CanGoForward, instance_id: {}", instance_id);
  return {true};
}

std::optional<FlutterError> WebviewFlutterPlugin::GoBack(int64_t instance_id) {
  spdlog::debug("[webview_flutter] GoBack, instance_id: {}", instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::GoForward(
    int64_t instance_id) {
  spdlog::debug("[webview_flutter] GoForward, instance_id: {}", instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::Reload(int64_t instance_id) {
  spdlog::debug("[webview_flutter] Reload, instance_id: {}", instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::ClearCache(
    int64_t instance_id,
    bool include_disk_files) {
  spdlog::debug(
      "[webview_flutter] ClearCache, instance_id: {}, include_disk_files: {}",
      instance_id, include_disk_files);
  return std::nullopt;
}

void WebviewFlutterPlugin::EvaluateJavascript(
    int64_t instance_id,
    const std::string& javascript_string,
    std::function<
        void(ErrorOr<std::optional<std::string>> reply)> /* result */) {
  spdlog::debug(
      "[webview_flutter] EvaluateJavascript, instance_id: {}, "
      "javascript_string: {}",
      instance_id, javascript_string);
}

std::optional<FlutterError> WebviewFlutterPlugin::Create(
    int64_t instance_id,
    const std::string& channel_name) {
  spdlog::debug("[webview_flutter] Create, instance_id: {}, channel_name: {}",
               instance_id, channel_name);
  return std::nullopt;
}

ErrorOr<std::optional<std::string>> WebviewFlutterPlugin::GetTitle(
    int64_t instance_id) {
  spdlog::debug("[webview_flutter] GetTitle, instance_id: {}", instance_id);
  return {std::nullopt};
}
std::optional<FlutterError> WebviewFlutterPlugin::ScrollTo(int64_t instance_id,
                                                           int64_t x,
                                                           int64_t y) {
  spdlog::debug("[webview_flutter] ScrollTo, instance_id: {}, x: {}, y: {}",
               instance_id, x, y);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::ScrollBy(int64_t instance_id,
                                                           int64_t x,
                                                           int64_t y) {
  spdlog::debug("[webview_flutter] ScrollBy, instance_id: {}, x: {}, y: {}",
               instance_id, x, y);
  return std::nullopt;
}

ErrorOr<int64_t> WebviewFlutterPlugin::GetScrollX(int64_t instance_id) {
  spdlog::debug("[webview_flutter] GetScrollX, instance_id: {}", instance_id);
  return {0};
}

ErrorOr<int64_t> WebviewFlutterPlugin::GetScrollY(int64_t instance_id) {
  spdlog::debug("[webview_flutter] GetScrollY, instance_id: {}", instance_id);
  return {0};
}

ErrorOr<WebViewPoint> WebviewFlutterPlugin::GetScrollPosition(
    int64_t instance_id) {
  spdlog::debug("[webview_flutter] GetScrollPosition, instance_id: {}",
               instance_id);
  return {WebViewPoint{0, 0}};
}

std::optional<FlutterError>
WebviewFlutterPlugin::SetWebContentsDebuggingEnabled(bool enabled) {
  spdlog::debug("[webview_flutter] SetWebContentsDebuggingEnabled, enabled: {}",
               enabled);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetWebViewClient(
    int64_t instance_id,
    int64_t web_view_client_instance_id) {
  spdlog::debug(
      "[webview_flutter] SetWebViewClient, instance_id: {}, "
      "web_view_client_instance_id: {}",
      instance_id, web_view_client_instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::AddJavaScriptChannel(
    int64_t instance_id,
    int64_t java_script_channel_instance_id) {
  spdlog::debug(
      "[webview_flutter] AddJavaScriptChannel, instance_id: {}, "
      "java_script_channel_instance_id: {}",
      instance_id, java_script_channel_instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::RemoveJavaScriptChannel(
    int64_t instance_id,
    int64_t java_script_channel_instance_id) {
  spdlog::debug(
      "[webview_flutter] RemoveJavaScriptChannel, instance_id: {}, "
      "java_script_channel_instance_id: {}",
      instance_id, java_script_channel_instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetDownloadListener(
    int64_t instance_id,
    const int64_t* listener_instance_id) {
  spdlog::debug(
      "[webview_flutter] SetDownloadListener, instance_id: {}, "
      "listener_instance_id: {}",
      instance_id, fmt::ptr(listener_instance_id));
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetWebChromeClient(
    int64_t instance_id,
    const int64_t* client_instance_id) {
  spdlog::debug(
      "[webview_flutter] SetWebChromeClient, instance_id: {}, "
      "client_instance_id: {}",
      instance_id, fmt::ptr(client_instance_id));
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetBackgroundColor(
    int64_t instance_id,
    int64_t color) {
  spdlog::debug(
      "[webview_flutter] SetWebChromeClient, instance_id: {}, color: 0x{:08}",
      instance_id, color);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::Create(
    int64_t instance_id,
    int64_t web_view_instance_id) {
  spdlog::debug(
      "[webview_flutter] Create, instance_id: {}, web_view_instance_id: {}",
      instance_id, web_view_instance_id);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetDomStorageEnabled(
    int64_t instance_id,
    bool flag) {
  spdlog::debug(
      "[webview_flutter] SetDomStorageEnabled, instance_id: {}, flag: {}",
      instance_id, flag);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterPlugin::SetJavaScriptCanOpenWindowsAutomatically(
    int64_t instance_id,
    bool flag) {
  spdlog::debug(
      "[webview_flutter] SetJavaScriptCanOpenWindowsAutomatically, "
      "instance_id: {}, flag: {}",
      instance_id, flag);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetSupportMultipleWindows(
    int64_t instance_id,
    bool support) {
  spdlog::debug(
      "[webview_flutter] SetSupportMultipleWindows, instance_id: {}. support: "
      "{}",
      instance_id, support);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetJavaScriptEnabled(
    int64_t instance_id,
    bool flag) {
  spdlog::debug(
      "[webview_flutter] SetJavaScriptEnabled, instance_id: {}, flag: {}",
      instance_id, flag);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetUserAgentString(
    int64_t instance_id,
    const std::string* user_agent_string) {
  spdlog::debug(
      "[webview_flutter] SetUserAgentString, instance_id: {}, "
      "user_agent_string: {}",
      instance_id, *user_agent_string);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterPlugin::SetMediaPlaybackRequiresUserGesture(int64_t instance_id,
                                                          bool require) {
  spdlog::debug(
      "[webview_flutter] SetMediaPlaybackRequiresUserGesture, instance_id: {}, "
      "require: {}",
      instance_id, require);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetSupportZoom(
    int64_t instance_id,
    bool support) {
  spdlog::debug("[webview_flutter] SetSupportZoom, instance_id: {}, support: {}",
               instance_id, support);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetLoadWithOverviewMode(
    int64_t instance_id,
    bool overview) {
  spdlog::debug(
      "[webview_flutter] SetLoadWithOverviewMode, instance_id: {}, overview: "
      "{}",
      instance_id, overview);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetUseWideViewPort(
    int64_t instance_id,
    bool use) {
  spdlog::debug("[webview_flutter] SetUseWideViewPort, instance_id: {}, use: {}",
               instance_id, use);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetDisplayZoomControls(
    int64_t instance_id,
    bool enabled) {
  spdlog::debug(
      "[webview_flutter] SetDisplayZoomControls, instance_id: {}, enabled: {}",
      instance_id, enabled);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetBuiltInZoomControls(
    int64_t instance_id,
    bool enabled) {
  spdlog::debug(
      "[webview_flutter] SetBuiltInZoomControls, instance_id: {}, enabled: {}",
      instance_id, enabled);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetAllowFileAccess(
    int64_t instance_id,
    bool enabled) {
  spdlog::debug(
      "[webview_flutter] SetAllowFileAccess, instance_id: {}, enabled: {}",
      instance_id, enabled);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetTextZoom(
    int64_t instance_id,
    int64_t text_zoom) {
  spdlog::debug("[webview_flutter] SetTextZoom, instance_id: {}, text_zoom: {}",
               instance_id, text_zoom);
  return std::nullopt;
}

ErrorOr<std::string> WebviewFlutterPlugin::GetUserAgentString(
    int64_t instance_id) {
  spdlog::debug("[webview_flutter] GetUserAgentString, instance_id: {}",
               instance_id);
  return {""};
}

std::optional<FlutterError>
WebviewFlutterPlugin::SetSynchronousReturnValueForOnShowFileChooser(
    int64_t instance_id,
    bool value) {
  spdlog::debug(
      "[webview_flutter] SetSynchronousReturnValueForOnShowFileChooser, "
      "instance_id: {}, value: {}",
      instance_id, value);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterPlugin::SetSynchronousReturnValueForOnConsoleMessage(
    int64_t instance_id,
    bool value) {
  spdlog::debug(
      "[webview_flutter] SetSynchronousReturnValueForOnConsoleMessage, "
      "instance_id: {}, value: {}",
      instance_id, value);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterPlugin::SetSynchronousReturnValueForShouldOverrideUrlLoading(
    int64_t instance_id,
    bool value) {
  spdlog::debug(
      "[webview_flutter] SetSynchronousReturnValueForShouldOverrideUrlLoading, "
      "instance_id: {}, value: {}",
      instance_id, value);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterPlugin::SetSynchronousReturnValueForOnJsAlert(int64_t instance_id,
                                                            bool value) {
  spdlog::debug(
      "[webview_flutter] SetSynchronousReturnValueForOnJsAlert, instance_id: "
      "{}, value: {}",
      instance_id, value);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterPlugin::SetSynchronousReturnValueForOnJsConfirm(
    int64_t instance_id,
    bool value) {
  spdlog::debug(
      "[webview_flutter] SetSynchronousReturnValueForOnJsConfirm, instance_id: "
      "{}, value: {}",
      instance_id, value);
  return std::nullopt;
}

std::optional<FlutterError>
WebviewFlutterPlugin::SetSynchronousReturnValueForOnJsPrompt(
    int64_t instance_id,
    bool value) {
  spdlog::debug(
      "[webview_flutter] SetSynchronousReturnValueForOnJsPrompt: instance_id: "
      "{}, value: {}",
      instance_id, value);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::AttachInstance(
    int64_t instance_identifier) {
  spdlog::debug("[webview_flutter] AttachInstance, instance_identifier: {}",
               instance_identifier);
  return std::nullopt;
}

std::optional<FlutterError> WebviewFlutterPlugin::SetCookie(
    int64_t identifier,
    const std::string& url,
    const std::string& value) {
  spdlog::debug(
      "[webview_flutter] SetCookie, identifier: {}, url: {}, value: {}",
      identifier, url, value);
  return std::nullopt;
}

void WebviewFlutterPlugin::RemoveAllCookies(
    int64_t identifier,
    const std::function<void(ErrorOr<bool> reply)> result) {
  spdlog::debug("[webview_flutter] RemoveAllCookies, identifier: {}",
               identifier);
  result(true);
}

std::optional<FlutterError> WebviewFlutterPlugin::SetAcceptThirdPartyCookies(
    int64_t identifier,
    int64_t web_view_identifier,
    bool accept) {
  spdlog::debug(
      "[webview_flutter] SetAcceptThirdPartyCookies, identifier: {}, "
      "web_view_identifier: {}, accept: {}",
      identifier, web_view_identifier, accept);
  return std::nullopt;
}

void WebviewPlatformView::on_resize(double width, double height, void* data) {
  if (const auto plugin = static_cast<WebviewPlatformView*>(data)) {
    plugin->width_ = static_cast<int32_t>(width);
    plugin->height_ = static_cast<int32_t>(height);
    spdlog::debug("[webview_flutter] Resize: {} {}", width, height);
  }
}

void WebviewPlatformView::on_set_direction(const int32_t direction,
                                           void* data) {
  if (auto plugin = static_cast<WebviewPlatformView*>(data)) {
    plugin->direction_ = direction;
    spdlog::debug("[webview_flutter] SetDirection: {}", plugin->direction_);
  }
}

void WebviewPlatformView::on_set_offset(const double left,
                                        const double top,
                                        void* data) {
  if (const auto plugin = static_cast<WebviewPlatformView*>(data)) {
    plugin->left_ = static_cast<int32_t>(left);
    plugin->top_ = static_cast<int32_t>(top);
    if (plugin->subsurface_) {
      spdlog::debug("[webview_flutter] SetOffset: left: {}, top: {}",
                   plugin->left_, plugin->top_);
      wl_subsurface_set_position(plugin->subsurface_, plugin->left_,
                                 plugin->top_);
      if (!plugin->callback_) {
        on_frame(plugin, plugin->callback_, 0);
      }
    }
  }
}

void WebviewPlatformView::on_touch(int32_t /* action */,
                                   int32_t /* point_count */,
                                   const size_t /* point_data_size */,
                                   const double* /* point_data */,
                                   void* /* data */) {
  // auto plugin = static_cast<WebviewFlutterPlugin*>(data);
}

void WebviewPlatformView::on_dispose(bool /* hybrid */, void* data) {
  const auto plugin = static_cast<WebviewPlatformView*>(data);
  if (plugin->callback_) {
    wl_callback_destroy(plugin->callback_);
    plugin->callback_ = nullptr;
  }

  if (plugin->subsurface_) {
    wl_subsurface_destroy(plugin->subsurface_);
    plugin->subsurface_ = nullptr;
  }

  if (plugin->surface_) {
    wl_surface_destroy(plugin->surface_);
    plugin->surface_ = nullptr;
  }
}

const struct platform_view_listener
    WebviewPlatformView::platform_view_listener_ = {
        .resize = on_resize,
        .set_direction = on_set_direction,
        .set_offset = on_set_offset,
        .on_touch = on_touch,
        .dispose = on_dispose};

void WebviewPlatformView::on_frame(void* data,
                                   wl_callback* callback,
                                   const uint32_t /* time */) {
  const auto obj = static_cast<WebviewPlatformView*>(data);

  obj->callback_ = nullptr;

  if (callback) {
    wl_callback_destroy(callback);
  }

  // TODO obj->DrawFrame(time);

  // Z-Order
  // wl_subsurface_place_above(obj->subsurface_, obj->parent_surface_);
  wl_subsurface_place_below(obj->subsurface_, obj->parent_surface_);

  obj->callback_ = wl_surface_frame(obj->surface_);
  wl_callback_add_listener(obj->callback_, &WebviewPlatformView::frame_listener,
                           data);

  wl_subsurface_set_position(obj->subsurface_, obj->left_, obj->top_);

  wl_surface_commit(obj->surface_);
}

const wl_callback_listener WebviewPlatformView::frame_listener = {.done =
                                                                      on_frame};

}  // namespace plugin_webview_flutter
