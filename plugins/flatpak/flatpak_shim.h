/*
 * Copyright 2020-2025 Toyota Connected North America
 * Copyright 2025 Ahmed Wafdy
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

#ifndef FLUTTER_PLUGIN_FLATPAK_FLATPAK_SHIM_H
#define FLUTTER_PLUGIN_FLATPAK_FLATPAK_SHIM_H

#include <filesystem>
#include <optional>
#include <string>

#define FLATPAK_EXTERN extern "C"
#include "flatpak.h"

#include <glib/garray.h>
#include <asio/io_context_strand.hpp>
#include <utility>

#include <flutter/event_channel.h>
#include "appstream_catalog.h"
#include "asio/steady_timer.hpp"
#include "component.h"
#include "messages.g.h"
#include "operation_tracker.h"
#include "portals/portal_manager.h"
#include "portals/permissions_portal/permissions_portal.h"

namespace flatpak_plugin {
class FlatpakPlugin;
class OperationTracker;

/**
 * \brief A utility class providing various helper functions for interacting
 * with Flatpak installations, remotes, and applications.
 */
struct FlatpakShim : std::enable_shared_from_this<FlatpakShim> {
  static constexpr size_t BUFFER_SIZE =
      32768;  ///< Buffer size used for decompressing.

  explicit FlatpakShim(FlatpakPlugin* plugin = nullptr,
                       flutter::BinaryMessenger* messenger = nullptr,
                       asio::io_context::strand* strand = nullptr)
      : plugin_(plugin), messenger_(messenger), strand_(strand) {
    if (strand_) {
      operation_tracker_ = std::make_unique<OperationTracker>(
          strand_->context(), [this](const flutter::EncodableMap& event) {
            this->SendTransactionEvent(
                "operation_tracker", const_cast<flutter::EncodableMap&>(event));
          });

      permissions_portal_ =
          std::make_unique<PermissionsPortal>(strand->context());
    }
  }

  ~FlatpakShim() {
    plugin_ = nullptr;
    messenger_ = nullptr;
  }

  /**
   * \brief Retrieves an optional attribute from an XML node.
   * \param node Pointer to the XML node.
   * \param attrName Name of the attribute to retrieve.
   * \return The attribute value as an optional string, or std::nullopt if not
   * found.
   */
  static std::optional<std::string> getOptionalAttribute(const xmlNode* node,
                                                         const char* attrName);

  /**
   * \brief Retrieves a required attribute from an XML node.
   * \param node Pointer to the XML node.
   * \param attrName Name of the attribute to retrieve.
   * \return The attribute value as a string.
   * \throws std::runtime_error if the attribute is not found.
   */
  static std::string getAttribute(const xmlNode* node, const char* attrName);

  /**
   * \brief Prints the details of a Component object to the standard output.
   * \param component The Component object to print.
   */
  static void PrintComponent(const Component& component);

  /**
   * \brief Retrieves the system installations available on the host.
   * \return A GPtrArray containing pointers to FlatpakInstallation objects.
   */
  static GPtrArray* get_system_installations();

  /**
   * \brief Retrieves the remotes associated with a given Flatpak installation.
   * \param installation Pointer to the FlatpakInstallation object.
   * \return A GPtrArray containing pointers to FlatpakRemote objects.
   */
  static GPtrArray* get_remotes(FlatpakInstallation* installation);

  /**
   * \brief Retrieves the AppStream timestamp from a given file path.
   * \param timestamp_filepath The path to the timestamp file.
   * \return The timestamp as a std::time_t value.
   */
  static std::time_t get_appstream_timestamp(
      const std::filesystem::path& timestamp_filepath);

  /**
   * \brief Updates the AppStream repo for all remotes.
   */
  static void update_appstream();

  /**
   * \brief Formats a time value into an ISO 8601 string.
   * \param raw_time The raw time value to format.
   * \param buffer The buffer to store the formatted string.
   * \param buffer_size The size of the buffer.
   */
  static void format_time_iso8601(time_t raw_time,
                                  char* buffer,
                                  size_t buffer_size);

  /**
   * \brief Retrieves the default languages for a Flatpak installation.
   * \param installation Pointer to the FlatpakInstallation object.
   * \return A flutter::EncodableList containing the default languages.
   */
  static flutter::EncodableList installation_get_default_languages(
      FlatpakInstallation* installation);

  /**
   * \brief Retrieves the default locales for a Flatpak installation.
   * \param installation Pointer to the FlatpakInstallation object.
   * \return A flutter::EncodableList containing the default locales.
   */
  static flutter::EncodableList installation_get_default_locales(
      FlatpakInstallation* installation);

  /**
   * \brief Converts a FlatpakInstallation object into an Installation object.
   * \param installation Pointer to the FlatpakInstallation object.
   * \return The corresponding Installation object.
   */
  static Installation get_installation(FlatpakInstallation* installation);

  /**
   * \brief Retrieves the content rating map for a FlatpakInstalledRef object.
   * \param ref Pointer to the FlatpakInstalledRef object.
   * \return A flutter::EncodableMap containing the content rating information.
   */
  static flutter::EncodableMap get_content_rating_map(FlatpakInstalledRef* ref);

  /**
   * \brief Populates a list of applications installed in a Flatpak
   * installation. \param installation Pointer to the FlatpakInstallation
   * object. \param application_list The list to populate with application
   * details.
   */
  static void get_application_list(FlatpakInstallation* installation,
                                   flutter::EncodableList& application_list);

  /**
   * \brief Retrieves the user installation.
   * \return An ErrorOr object containing the Installation or an error.
   */
  static ErrorOr<Installation> GetUserInstallation();

  /**
   * \brief Retrieves the system installations as an EncodableList.
   * \return An ErrorOr object containing the EncodableList or an error.
   */
  static ErrorOr<flutter::EncodableList> GetSystemInstallations();

  /**
   * \brief Retrieves the list of installed applications.
   * \return An ErrorOr object containing the EncodableList or an error.
   */
  static ErrorOr<flutter::EncodableList> GetApplicationsInstalled();

  /**
   * \brief Retrieves the list of applications that need to update.
   * \return An ErrorOr object containing the EncodableList or an error.
   */
  static ErrorOr<flutter::EncodableList> GetApplicationsUpdate();

  /**
   * \brief Retrieves the list of applications in a remote.
   * \param id ID of the remote.
   * \return EncodableList of all applications in that remote.
   */
  static ErrorOr<flutter::EncodableList> GetApplicationsRemote(
      const std::string& id);

  /**
   * \brief Adding remote for flatpak.
   * \param configuration Configurations of the remote wanted to add, name and
   * URL is a must. \return An ErrorOr object containing the EncodableList or an
   * error.
   */
  static ErrorOr<bool> RemoteAdd(const Remote& configuration);

  /**
   * \brief Removing remote from flatpak.
   * \param id id of the remote wanted to remove, name and URL are a must.
   * \return An ErrorOr object containing the EncodableList or an error.
   */
  static ErrorOr<bool> RemoteRemove(const std::string& id);

  /**
   * \brief Install flatpak application async.
   * \param id id of the application wanted to install.
   * \param callback Callback invoked when installation completes or fails.
   */
  void ApplicationInstall(const std::string& id,
                          const std::function<void(ErrorOr<bool>)>& callback);

  /**
   * \brief Uninstall flatpak application.
   * \param id id of the application wanted to uninstall.
   * \param callback Callback invoked when installation completes or fails.
   * \return An ErrorOr object containing the EncodableList or an error.
   */
  void ApplicationUninstall(const std::string& id,
                            const std::function<void(ErrorOr<bool>)>& callback);

  /**
   * \brief Update flatpak application async.
   * \param id id of the application wanted to install.
   * \param callback Callback invoked when installation completes or fails.
   */
  void ApplicationUpdate(const std::string& id,
                         const std::function<void(ErrorOr<bool>)>& callback);

  /**
   * \brief Start flatpak application.
   * \param id Id of the application to start application.
   * \param strand Asio strand to execute async operations.
   * \param portal_manager Ptr to portal manager to execute portal operations.
   * \param completion_callback Callback function for async operations.
   * \return An ErrorOr object containing the EncodableList or an
   * error.
   */
  void ApplicationStart(
      const std::string& id,
      asio::io_context::strand& strand,
      const std::shared_ptr<PortalManager>& portal_manager,
      const std::function<void(const ErrorOr<bool>&)>& completion_callback);

  /**
   * \brief Stop flatpak Application.
   * \param id id of the application to stop.
   * \return An ErrorOr object containing the EncodableList or an error.
   */
  static ErrorOr<bool> ApplicationStop(const std::string& id);

  /**
   * \brief Retrieves the remotes for a given installation ID.
   * \param installation_id The ID of the installation.
   * \return An ErrorOr object containing the EncodableList or an error.
   */
  static ErrorOr<flutter::EncodableList> get_remotes_by_installation_id(
      const std::string& installation_id);

  /**
   * \brief Find which remote contains an app with specific details.
   * \param installation Installation that contains the app.
   * \param app_name Application name.
   * \param app_arch Application arch.
   * \param app_branch Application branch.
   * \return Remote string that has the app.
   */
  static std::string find_remote_for_app(FlatpakInstallation* installation,
                                         const char* app_name,
                                         const char* app_arch,
                                         const char* app_branch);

  /**
   * \brief Search for app ID  in all remotes.
   * \param installation Installation that contains the App.
   * \param app_id Application id.
   * \return A Pair of two strings contains a remote name and application ref.
   */
  static std::pair<std::string, std::string> find_app_in_remotes(
      FlatpakInstallation* installation,
      const std::string& app_id);

  static std::pair<std::string, std::string> find_app_in_remotes_fallback(
      FlatpakInstallation* installation,
      const std::string& app_id);

  static std::pair<std::string, std::string> search_in_single_remote(
      FlatpakInstallation* installation,
      const char* remote_name,
      const std::string& app_id);

  /**
   * \brief Converts a FlatpakRemoteType to its string representation.
   * \param type The FlatpakRemoteType to convert.
   * \return The string representation of the remote type.
   */
  static std::string FlatpakRemoteTypeToString(FlatpakRemoteType type);

  /**
   * \brief Converts a list of remotes into an EncodableList.
   * \param remotes Pointer to a GPtrArray containing the remotes.
   * \return A flutter::EncodableList containing the remotes.
   */
  static flutter::EncodableList convert_remotes_to_EncodableList(
      const GPtrArray* remotes);

  /**
   * \brief Converts a list of applications into an EncodableList.
   * \param applications Pointer to a GPtrArray contains the applications.
   * \param remote Pointer to a remote ref to process.
   * \return A flutter::EncodableList containing the remotes.
   */
  static flutter::EncodableList convert_applications_to_EncodableList(
      const GPtrArray* applications,
      FlatpakRemote* remote);

  /**
   * \brief Retrieves the metadata of a FlatpakInstalledRef as a string.
   * \param installed_ref Pointer to the FlatpakInstalledRef object.
   * \return The metadata as a string.
   */
  static std::string get_metadata_as_string(FlatpakInstalledRef* installed_ref);

  /**
   * \brief Retrieves the appdata of a FlatpakInstalledRef as a string.
   * \param installed_ref Pointer to the FlatpakInstalledRef object.
   * \return The appdata as a string.
   */
  static std::string get_appdata_as_string(FlatpakInstalledRef* installed_ref);

  /**
   * \brief Decompresses a GZip-compressed data buffer.
   * \param compressedData The compressed data buffer.
   * \param decompressedData The buffer to store the decompressed data.
   * \return A vector containing the decompressed data.
   */
  static std::vector<char> decompress_gzip(
      const std::vector<char>& compressedData,
      std::vector<char>& decompressedData);

  /**
   * \brief Stop monitoring all running Applications
   */
  static void stop_all_monitoring();

  /**
   * \brief Checking status of application if app is running or not.
   * \param app_name Name of application.
   * \return True or false depending on application status if the app is running
   * or not.
   */
  static bool is_app_running(const std::string& app_name);

  /**
   * \brief Sets up the event channel for transaction events.
   * \param messenger Pointer to the BinaryMessenger used for communication.
   * \param id Id of the application.
   */
  void SetupTransactionEventChannel(const std::string& id,
                                    flutter::BinaryMessenger* messenger);

  /**
   * \brief Sets up the event channel for Permissions Access events.
   * \param messenger Pointer to the BinaryMessenger used for communication.
   * \param strand Asio strand to execute async operations.
   * portal with event callback.
   */
  void SetupAccessEventChannel(flutter::BinaryMessenger* messenger,
                               const asio::io_context::strand& strand);

  /**
   * \brief Handles All methods responses comes from the flutter side.
   * @param method_call A flutter method call for Method channel implementation.
   * @param result A pointer for result implementation.
   */
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

 private:
  struct sandbox {
    struct application {
      std::string name;
      std::string runtime;
      std::string sdk;
      std::string base;
      std::string command;
    } application;

    struct context {
      std::vector<std::string> shared;
      std::vector<std::string> sockets;
      std::vector<std::string> devices;
      std::vector<std::string> filesystems;
    } context;

    struct extra {
      std::string name;
      std::string checksum;
      std::string size;
      std::string uri;
    } extra;

    std::vector<std::string> session_bus;
    std::vector<std::string> system_bus;
    std::map<std::string, std::string> environment;
    std::vector<std::string> extensions;
    std::vector<std::string> built_extensions;
  };
  struct MonitorSession {
    std::string name;
    pid_t pid;
    std::shared_ptr<asio::steady_timer> timer;
    std::weak_ptr<PortalManager> portal_manager;
    FlatpakInstance* instance;
    std::atomic<bool> cancelled{false};
    asio::io_context::strand* strand;

    MonitorSession(asio::io_context::strand* strand_ptr,
                   std::string name,
                   pid_t pid,
                   const std::shared_ptr<PortalManager>& p_m,
                   FlatpakInstance* inst)
        : name(std::move(name)),
          pid(pid),
          timer(std::make_shared<asio::steady_timer>(strand_ptr->context())),
          portal_manager(p_m),
          instance(inst),
          strand(strand_ptr) {}

    ~MonitorSession() {
      if (instance) {
        g_object_unref(instance);
        instance = nullptr;
      }
    }

    MonitorSession(const MonitorSession&) = delete;
    MonitorSession& operator=(const MonitorSession&) = delete;

    [[nodiscard]] bool is_still_running() const {
      if (!instance)
        return false;
      if (!flatpak_instance_is_running(instance))
        return false;
      return flatpak_instance_get_pid(instance) == pid;
    }
  };

  struct PermissionRequest {
    std::string app_id;
    std::vector<std::string> requests;
    std::map<std::string, bool> result;
    size_t permission_index = 0;
    std::function<void(const std::map<std::string, bool>&)> callback;
    std::chrono::steady_clock::time_point created_at;
  };

  template <typename T>
  struct GObjectGuard {
    T* obj;
    explicit GObjectGuard(T* o) : obj(o) {}
    ~GObjectGuard() {
      if (obj)
        g_object_unref(obj);
    }
    T* get() const { return obj; }
    T* release() {
      T* tmp = obj;
      obj = nullptr;
      return tmp;
    }
  };

  static std::mutex monitor_mutex_;
  static std::map<std::string, std::shared_ptr<MonitorSession>>
      active_sessions_;
  FlatpakPlugin* plugin_;
  flutter::BinaryMessenger* messenger_;
  asio::io_context::strand* strand_;

  std::unique_ptr<OperationTracker> operation_tracker_;

  // Event channel for streaming transaction progress to Flutter
  std::map<std::string,
           std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>>
      transactions_event_channels_;
  std::map<std::string,
           std::shared_ptr<flutter::EventSink<flutter::EncodableValue>>>
      transactions_event_sinks_;
  mutable std::mutex event_sink_mutex_;

  // Event channel for streaming permissions access to the flutter widget
  std::unique_ptr<PermissionsPortal> permissions_portal_;
  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>
      access_event_channel_;
  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> access_sink_;
  mutable std::mutex access_sink_mutex_;
  std::map<std::string, PermissionRequest> active_permissions_;
  mutable std::mutex permissions_mutex_;

  static std::optional<Application> create_component(
      FlatpakRemoteRef* app_ref,
      const std::optional<AppstreamCatalog>& app_catalog);

  static std::string create_metadata(const Component& component);

  static std::string create_appdata(const Component& component);

  static void create_sandbox(FlatpakInstalledRef* installed_ref,
                             const asio::io_context::strand& strand,
                             const std::function<void(bool)>& callback,
                             PortalManager* portal_manager);

  static sandbox parse_metadata(const std::string& metadata);

  static std::map<std::string, std::string> extract_metadataSections(
      const std::string& metadata,
      const std::string& section);

  static void setup_portal_sessions(
      const sandbox& configs,
      const asio::io_context::strand& strand,
      PortalManager* portal_manager,
      const std::function<void(ErrorOr<bool>)>& callback);

  static void monitor_app(const std::shared_ptr<MonitorSession>& session);

  static void cleanup_app(const std::shared_ptr<MonitorSession>& session);

  static void schedule_next_check(
      const std::shared_ptr<MonitorSession>& session);

  static FlatpakInstance* find_running_instance(const std::string& app_id);

  void check_runtime(FlatpakInstalledRef* installed_ref,
                     FlatpakInstallation* installation,
                     asio::io_context::strand& strand,
                     const std::function<void(ErrorOr<bool>)>& callback);

  void install_runtime(
      const std::string& runtime,
      const std::string& remote,
      asio::io_context::strand& strand,
      FlatpakInstallation* installation,
      const std::function<void(ErrorOr<bool>)>& complete_callback);

  static bool is_runtime_installed_for_app(const std::string& runtime,
                                           FlatpakInstallation* installation);

  void install_extensions(
      const std::vector<std::string>& extensions,
      const std::string& remote,
      asio::io_context::strand& strand,
      FlatpakInstallation* installation,
      const std::function<void(ErrorOr<bool>)>& complete_callback);

  // flutter streaming callback functions
  void SendTransactionEvent(const std::string& id,
                            flutter::EncodableMap& event) const;

  // flutter streaming callback functions
  void SendPermissionEvent(const flutter::EncodableMap& event,
                           const std::function<void(bool)>& callback);

  // callback triggered by "changed" signal
  static void OnProgressChanged(FlatpakTransactionProgress* progress,
                                gpointer user_data);

  // callback when there is a new operation in transaction
  static void OnNewOperation(FlatpakTransaction* transaction,
                             FlatpakTransactionOperation* operation,
                             FlatpakTransactionProgress* progress,
                             gpointer user_data);

  // callback when transaction operation completed
  static void OnOperationComplete(FlatpakTransaction* transaction,
                                  FlatpakTransactionOperation* operation,
                                  const char* commit,
                                  FlatpakTransactionResult result,
                                  gpointer user_data);

  // callback when transaction operation reports an error
  static gboolean OnOperationError(FlatpakTransaction* transaction,
                                   FlatpakTransactionOperation* operation,
                                   const GError* error,
                                   FlatpakTransactionErrorDetails error_details,
                                   gpointer user_data);
  static gboolean OnTransactionReady(FlatpakTransaction* transaction,
                                     gpointer user_data);

  bool check_disk_usage(FlatpakInstallation* installation,
                        guint64 estimated_download,
                        const std::string& app_id);

  void RequestAppLaunchPermission(
      const std::string& app_id,
      FlatpakInstalledRef* installed_ref,
      const std::function<void(const std::map<std::string, bool>&)>& callback);

  void HandlePermissionResponse(const std::string& request_id,
                                const std::string& permission,
                                bool granted);

  void ShowNextDialog(const std::string& request_id);

  static std::string GenerateRequestId();

  void CheckExistingPermissions(
      const std::string& app_id,
      const std::vector<std::string>& permissions,
      const std::function<void(std::map<std::string, bool>)>& callback) const;

  void RemoveTransactionEvent(const std::string& id);
};

}  // namespace flatpak_plugin

#endif  // FLUTTER_PLUGIN_FLATPAK_FLATPAK_SHIM_H
