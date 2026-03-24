#include <asio/steady_timer.hpp>
#include <fstream>
#include <future>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include <flutter/encodable_value.h>

#include "../component.h"
#include "../flatpak_shim.h"
#include "spdlog/spdlog.h"

using namespace flatpak_plugin;
class TestBinaryMessenger : public flutter::BinaryMessenger {
 public:
  void Send(const std::string& /*channel*/,
            const uint8_t* /*message*/,
            size_t /*message_size*/,
            flutter::BinaryReply reply) const override {
    // No-op for tests
    if (reply) {
      reply(nullptr, 0);
    }
  }

  void SetMessageHandler(const std::string& channel,
                         flutter::BinaryMessageHandler handler) override {
    handlers_[channel] = std::move(handler);
  }

 private:
  mutable std::map<std::string, flutter::BinaryMessageHandler> handlers_;
};

class FlatpakPluginTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Initialize test messenger
    test_messenger_ = std::make_unique<TestBinaryMessenger>();
  }

  void TearDown() override { test_messenger_.reset(); }

  [[nodiscard]] flutter::BinaryMessenger* GetTestMessenger() const {
    return test_messenger_.get();
  }

 private:
  std::unique_ptr<TestBinaryMessenger> test_messenger_;
};

// Minimal test messenger implementation

class ComponentTest : public ::testing::Test {
 protected:
  void SetUp() override { xmlInitParser(); }

  void TearDown() override { xmlCleanupParser(); }

  static xmlDocPtr createXmlDoc(const std::string& xmlContent) {
    return xmlParseMemory(xmlContent.c_str(),
                          static_cast<int>(xmlContent.length()));
  }

  static xmlNodePtr getRootElement(xmlDocPtr doc) {
    return xmlDocGetRootElement(doc);
  }

  static std::string componentXml() {
    return R"(
<component type="desktop">
    <id>app.authpass.AuthPass</id>
    <name>AuthPass</name>
    <summary>Password Manager: Keep your passwords safe across all platforms and devices</summary>
    <developer_name>Herbert Poul</developer_name>
    <description>
        <p>Easily and securely keep track of all your Passwords!</p>
        <p>AuthPass is a stand alone password manager with support for the popular and proven KeePass format.</p>
        <ul>
            <li>All your passwords in one place.</li>
            <li>Generate secure random passwords for each of your accounts.</li>
            <li>Keep track of your accounts across the web.</li>
        </ul>
    </description>
    <icon height="64" type="cached" width="64">app.authpass.AuthPass.png</icon>
    <icon height="128" type="cached" width="128">app.authpass.AuthPass.png</icon>
    <categories>
        <category>Security</category>
        <category>Utility</category>
    </categories>
    <kudos>
        <kudo>HiDpiIcon</kudo>
    </kudos>
    <project_license>GPL-3.0-or-later</project_license>
    <url type="homepage">https://authpass.app/</url>
    <url type="bugtracker">https://github.com/authpass/authpass/issues</url>
    <url type="donation">https://github.com/sponsors/hpoul</url>
    <url type="translate">https://translate.authpass.app</url>
    <screenshots>
        <screenshot type="default">
            <image type="source">https://data.authpass.app/data/screenshot_composition_small.png</image>
            <image height="351" type="thumbnail" width="624">https://dl.flathub.org/repo/screenshots/app.authpass.AuthPass-stable/624x351/app.authpass.AuthPass-8e73a9934daf432df01694fc5aa494e5.png</image>
        </screenshot>
    </screenshots>
    <content_rating type="oars-1.1">
        <content_attribute id="violence-cartoon">mild</content_attribute>
        <content_attribute id="language-profanity">moderate</content_attribute>
        <content_attribute id="social-info">none</content_attribute>
    </content_rating>
    <releases>
        <release timestamp="1654819200" version="1.9.6_1904">
            <description>
                <p>Bug fixes and improvements</p>
            </description>
        </release>
        <release timestamp="1650000000" version="1.9.5">
            <description>
                <p>Previous release</p>
            </description>
        </release>
    </releases>
    <launchable type="desktop-id">app.authpass.AuthPass.desktop</launchable>
    <metadata>
        <value key="flathub::build::build_log_url">https://buildbot.flathub.org/#/builders/6/builds/97962</value>
    </metadata>
    <bundle type="flatpak" runtime="org.freedesktop.Platform/x86_64/23.08" sdk="org.freedesktop.Sdk/x86_64/23.08">app/app.authpass.AuthPass/x86_64/stable</bundle>
    <keywords>
        <keyword>password</keyword>
        <keyword>security</keyword>
        <keyword>keepass</keyword>
    </keywords>
    <languages>
        <lang percentage="100">en</lang>
        <lang percentage="80">de</lang>
        <lang percentage="70">fr</lang>
    </languages>
</component>
        )";
  }
};

TEST_F(ComponentTest, Component_CompleteParsingTest) {
  std::string xmlContent = componentXml();
  xmlDocPtr doc = createXmlDoc(xmlContent);
  ASSERT_NE(doc, nullptr);

  xmlNodePtr root = getRootElement(doc);
  ASSERT_NE(root, nullptr);

  Component component(root, "en");

  EXPECT_EQ(component.getId(), "app.authpass.AuthPass");
  EXPECT_EQ(component.getName(), "AuthPass");
  EXPECT_EQ(component.getSummary(),
            "Password Manager: Keep your passwords safe across all platforms "
            "and devices");

  EXPECT_TRUE(component.getProjectLicense().has_value());
  EXPECT_EQ(component.getProjectLicense().value(), "GPL-3.0-or-later");

  EXPECT_TRUE(component.getDescription().has_value());
  EXPECT_TRUE(component.getDescription().value().find(
                  "Easily and securely keep track") != std::string::npos);

  // categories
  EXPECT_TRUE(component.getCategories().has_value());
  const auto& categories = component.getCategories().value();
  EXPECT_EQ(categories.size(), 2);
  EXPECT_TRUE(categories.find("Security") != categories.end());
  EXPECT_TRUE(categories.find("Utility") != categories.end());

  // keywords
  EXPECT_TRUE(component.getKeywords().has_value());
  const auto& keywords = component.getKeywords().value();
  EXPECT_EQ(keywords.size(), 3);
  EXPECT_TRUE(keywords.find("password") != keywords.end());
  EXPECT_TRUE(keywords.find("security") != keywords.end());
  EXPECT_TRUE(keywords.find("keepass") != keywords.end());

  // languages
  EXPECT_TRUE(component.getLanguages().has_value());
  const auto& languages = component.getLanguages().value();
  EXPECT_EQ(languages.size(), 3);
  EXPECT_TRUE(languages.find("en") != languages.end());
  EXPECT_TRUE(languages.find("de") != languages.end());
  EXPECT_TRUE(languages.find("fr") != languages.end());

  // screenshots
  EXPECT_TRUE(component.getScreenshots().has_value());
  const auto& screenshots = component.getScreenshots().value();
  EXPECT_EQ(screenshots.size(), 1);
  EXPECT_TRUE(screenshots[0].getType().has_value());
  EXPECT_EQ(screenshots[0].getType().value(), "default");

  // icons
  EXPECT_TRUE(component.getIcons().has_value());
  const auto& icons = component.getIcons().value();
  EXPECT_EQ(icons.size(), 2);

  // launchable
  EXPECT_TRUE(component.getLaunchable().has_value());
  const auto& launchable = component.getLaunchable().value();
  EXPECT_EQ(launchable.size(), 1);
  EXPECT_TRUE(launchable.find("app.authpass.AuthPass.desktop") !=
              launchable.end());

  // bundle
  EXPECT_TRUE(component.getBundle().has_value());
  EXPECT_EQ(component.getBundle().value(),
            "app/app.authpass.AuthPass/x86_64/stable");

  xmlFreeDoc(doc);
}

TEST_F(FlatpakPluginTest, GetUserInstallationsTest) {
  const auto result = FlatpakShim::GetUserInstallation();

  EXPECT_FALSE(result.value().id().empty());
  EXPECT_FALSE(result.value().display_name().empty());
}

TEST_F(FlatpakPluginTest, GetSystemInstallationsTest) {
  const auto result = FlatpakShim::GetSystemInstallations();
  auto& system_installations = result.value();
  for (auto& system_installation : system_installations) {
    EXPECT_TRUE(std::holds_alternative<flutter::CustomEncodableValue>(
        system_installation));
  }
  EXPECT_GT(result.value().size(), 0);
}

TEST_F(FlatpakPluginTest, AddRemoteTest) {
  const auto remote = Remote(
      "full-remote", "https://full.example.com/repo", "", "Full Test Remote",
      "Comprehensive test comment", "Detailed test description",
      "https://full.example.com", "https://full.example.com/icon", "22.08",
      "org.example.App", "oci", "runtime/*", "2024-01-01T00:00:00Z",
      "/var/lib/flatpak/appstream", false, true, true, false, 10);

  auto result = FlatpakShim::RemoteAdd(remote);
  ASSERT_FALSE(result.has_error());
  EXPECT_TRUE(result.value());
  if (result.value()) {
    auto cleanremote = FlatpakShim::RemoteRemove("full-remote");
  }
}

TEST_F(FlatpakPluginTest, AddEmptyRemoteTest) {
  const auto remote = Remote("", "", "", "", "", "", "", "", "", "", "", "", "",
                             "", false, false, false, false, 1);

  const auto result = FlatpakShim::RemoteAdd(remote);
  EXPECT_TRUE(result.has_error());
}

// Install a real application and uninstall it in another test to clean
// the environment.
TEST_F(FlatpakPluginTest, ApplicationInstallTest) {
  auto io_context = std::make_shared<asio::io_context>();
  auto work_guard = asio::make_work_guard(*io_context);
  auto strand = std::make_shared<asio::io_context::strand>(*io_context);

  std::thread io_thread([io_context]() { io_context->run(); });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  struct PromiseGuard {
    std::shared_ptr<std::promise<ErrorOr<bool>>> promise;
    std::once_flag flag;

    void set_value(ErrorOr<bool> value) {
      std::call_once(flag, [this, value = std::move(value)]() mutable {
        try {
          promise->set_value(std::move(value));
        } catch (...) {
        }
      });
    }
  };

  auto guard = std::make_shared<PromiseGuard>();
  guard->promise = std::make_shared<std::promise<ErrorOr<bool>>>();
  auto future = guard->promise->get_future();

  auto messenger = GetTestMessenger();

  auto shim = std::make_shared<FlatpakShim>(nullptr, messenger, strand.get());

  auto app_id = "org.telegram.desktop";

  shim->ApplicationInstall(app_id, [guard](const ErrorOr<bool>& result) {
    guard->set_value(result);
  });

  auto status = future.wait_for(std::chrono::minutes(10));

  ASSERT_NE(status, std::future_status::timeout)
      << "Installation timed out after 10 minutes";

  auto result = future.get();

  ASSERT_TRUE(result.value())
      << "Installation failed: " << result.error().message();

  GError* error = nullptr;
  auto installation = flatpak_installation_new_user(nullptr, &error);
  ASSERT_FALSE(error) << "Failed to get installation";

  auto refs =
      flatpak_installation_list_installed_refs(installation, nullptr, &error);
  ASSERT_FALSE(error) << "Failed to list installed refs";

  bool found = false;
  for (guint i = 0; i < refs->len; i++) {
    auto ref = static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));
    const char* ref_name = flatpak_ref_get_name(FLATPAK_REF(ref));
    if (ref_name && app_id == std::string(ref_name)) {
      found = true;
      break;
    }
  }

  g_ptr_array_unref(refs);
  g_object_unref(installation);

  EXPECT_TRUE(found) << "App " << app_id << " not found in installed refs";

  std::this_thread::sleep_for(std::chrono::seconds(2));

  work_guard.reset();
  io_context->stop();

  if (io_thread.joinable()) {
    io_thread.join();
  }
}

TEST_F(FlatpakPluginTest, ApplicationUninstallTest) {
  auto io_context = std::make_shared<asio::io_context>();
  auto work_guard = asio::make_work_guard(*io_context);
  auto strand = std::make_shared<asio::io_context::strand>(*io_context);

  std::thread io_thread([io_context]() { io_context->run(); });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  struct PromiseGuard {
    std::shared_ptr<std::promise<ErrorOr<bool>>> promise;
    std::once_flag flag;

    void set_value(ErrorOr<bool> value) {
      std::call_once(flag, [this, value = std::move(value)]() mutable {
        try {
          promise->set_value(std::move(value));
        } catch (...) {
        }
      });
    }
  };

  auto guard = std::make_shared<PromiseGuard>();
  guard->promise = std::make_shared<std::promise<ErrorOr<bool>>>();
  auto future = guard->promise->get_future();

  auto messenger = GetTestMessenger();

  auto shim = std::make_shared<FlatpakShim>(nullptr, messenger, strand.get());

  auto app_id = "org.telegram.desktop";

  shim->ApplicationUninstall(app_id, [guard](const ErrorOr<bool>& result) {
    guard->set_value(result);
  });

  auto status = future.wait_for(std::chrono::minutes(10));

  ASSERT_NE(status, std::future_status::timeout)
      << "Uninstallation timed out after 10 minutes";

  auto result = future.get();

  ASSERT_TRUE(result.value())
      << "Uninstallation failed: " << result.error().message();

  GError* error = nullptr;
  auto installation = flatpak_installation_new_user(nullptr, &error);
  ASSERT_FALSE(error) << "Failed to get installation";

  auto refs =
      flatpak_installation_list_installed_refs(installation, nullptr, &error);
  ASSERT_FALSE(error) << "Failed to list installed refs";

  bool found = false;
  for (guint i = 0; i < refs->len; i++) {
    auto ref = static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));
    const char* ref_name = flatpak_ref_get_name(FLATPAK_REF(ref));
    if (ref_name && app_id == std::string(ref_name)) {
      found = true;
      break;
    }
  }

  g_ptr_array_unref(refs);
  g_object_unref(installation);

  // should expect false
  EXPECT_FALSE(found) << "App " << app_id << " not found in installed refs";

  std::this_thread::sleep_for(std::chrono::seconds(2));

  work_guard.reset();
  io_context->stop();

  if (io_thread.joinable()) {
    io_thread.join();
  }
}

TEST_F(FlatpakPluginTest, ApplicationUninstallInvalidTest) {
  auto io_context = std::make_shared<asio::io_context>();
  auto work_guard = asio::make_work_guard(*io_context);
  auto strand = std::make_shared<asio::io_context::strand>(*io_context);

  std::thread io_thread([io_context]() { io_context->run(); });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  struct PromiseGuard {
    std::shared_ptr<std::promise<ErrorOr<bool>>> promise;
    std::once_flag flag;

    void set_value(ErrorOr<bool> value) {
      std::call_once(flag, [this, value = std::move(value)]() mutable {
        try {
          promise->set_value(std::move(value));
        } catch (...) {
        }
      });
    }
  };

  auto guard = std::make_shared<PromiseGuard>();
  guard->promise = std::make_shared<std::promise<ErrorOr<bool>>>();
  auto future = guard->promise->get_future();

  auto messenger = GetTestMessenger();

  auto shim = std::make_shared<FlatpakShim>(nullptr, messenger, strand.get());

  auto app_id = "invalid.app.test";

  shim->ApplicationUninstall(app_id, [guard](const ErrorOr<bool>& result) {
    guard->set_value(result);
  });

  auto status = future.wait_for(std::chrono::minutes(10));

  ASSERT_NE(status, std::future_status::timeout)
      << "Uninstallation timed out after 10 minutes";

  auto result = future.get();

  ASSERT_TRUE(result.has_error())
      << "Uninstallation failed: " << result.error().message();

  GError* error = nullptr;
  auto installation = flatpak_installation_new_user(nullptr, &error);
  ASSERT_FALSE(error) << "Failed to get installation";

  auto refs =
      flatpak_installation_list_installed_refs(installation, nullptr, &error);
  ASSERT_FALSE(error) << "Failed to list installed refs";

  bool found = false;
  for (guint i = 0; i < refs->len; i++) {
    auto ref = static_cast<FlatpakInstalledRef*>(g_ptr_array_index(refs, i));
    const char* ref_name = flatpak_ref_get_name(FLATPAK_REF(ref));
    if (ref_name && app_id == std::string(ref_name)) {
      found = true;
      break;
    }
  }

  g_ptr_array_unref(refs);
  g_object_unref(installation);

  EXPECT_TRUE(found) << "App " << app_id << " not found in installed refs";

  std::this_thread::sleep_for(std::chrono::seconds(2));

  work_guard.reset();
  io_context->stop();

  if (io_thread.joinable()) {
    io_thread.join();
  }
}

TEST_F(FlatpakPluginTest, GetRemoteAppsTest) {
  const auto apps = FlatpakShim::GetApplicationsRemote("flathub");

  EXPECT_GT(apps.value().size(), 0);
}

TEST_F(FlatpakPluginTest, GetApplicationsInstalledTest) {
  const auto apps = FlatpakShim::GetApplicationsInstalled();

  EXPECT_GT(apps.value().size(), 0);
}

TEST_F(FlatpakPluginTest, FindAppInRemoteSearchTest) {
  GError* error = nullptr;
  FlatpakInstallation* user_installation =
      flatpak_installation_new_user(nullptr, &error);
  auto [key, value] =
      FlatpakShim::find_app_in_remotes(user_installation, "com.spotify.Client");
  // assuming flathub remote is added
  EXPECT_FALSE(value.empty());
  EXPECT_EQ(key, "flathub");
}

TEST_F(FlatpakPluginTest, RunAppTest) {
  auto io_context = std::make_shared<asio::io_context>();
  auto work_guard = asio::make_work_guard(*io_context);
  auto strand = std::make_shared<asio::io_context::strand>(*io_context);

  std::thread io_thread([io_context]() {
    pthread_setname_np(pthread_self(), "test-io-thread");
    io_context->run();
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  struct PromiseGuard {
    std::shared_ptr<std::promise<ErrorOr<bool>>> promise;
    std::once_flag flag;

    void set_value(ErrorOr<bool> value) {
      std::call_once(flag, [this, &value]() {
        try {
          promise->set_value(std::move(value));
        } catch (const std::exception&) {
        }
      });
    }
  };

  auto guard = std::make_shared<PromiseGuard>();
  guard->promise = std::make_shared<std::promise<ErrorOr<bool>>>();
  auto future = guard->promise->get_future();
  auto portal_manager = std::make_shared<PortalManager>(*io_context);

  auto messenger = GetTestMessenger();
  auto shim = std::make_shared<FlatpakShim>(nullptr, messenger, strand.get());

  auto id = "com.visualstudio.code";  // net.lutris.Lutris // com.spotify.Client
                                      // // com.valvesoftware.Steam //
                                      // org.telegram.desktop
  shim->SetupAccessEventChannel(messenger, *strand);
  shim->ApplicationStart(
      id, *strand, portal_manager,
      [guard](const ErrorOr<bool>& result) { guard->set_value(result); });

  auto status = future.wait_for(std::chrono::minutes(2));

  ASSERT_NE(status, std::future_status::timeout)
      << "Installation timed out after 2 minutes";

  auto result = future.get();

  std::this_thread::sleep_for(std::chrono::minutes(2));

  bool running = FlatpakShim::is_app_running(id);
  EXPECT_TRUE(running);

  auto stop_result = FlatpakShim::ApplicationStop(id);
  ASSERT_TRUE(stop_result.value());

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  running = FlatpakShim::is_app_running(id);
  EXPECT_FALSE(running);

  work_guard.reset();
  io_context->stop();

  if (io_thread.joinable()) {
    io_thread.join();
  }
}

TEST_F(FlatpakPluginTest, RunMultipleAppsTest) {
  auto io_context = std::make_shared<asio::io_context>();
  auto work_guard = asio::make_work_guard(*io_context);
  auto strand = std::make_shared<asio::io_context::strand>(*io_context);
  auto portal_manager = std::make_shared<PortalManager>(*io_context);

  std::thread io_thread([io_context]() {
    pthread_setname_np(pthread_self(), "asio_worker");
    io_context->run();
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::vector<std::string> apps = {"md.obsidian.Obsidian", "com.spotify.Client",
                                   "com.visualstudio.code"};
  auto messenger = GetTestMessenger();

  struct PromiseGuard {
    std::shared_ptr<std::promise<ErrorOr<bool>>> promise;
    std::once_flag flag;

    void set_value(ErrorOr<bool> value) {
      std::call_once(flag, [this, value = std::move(value)]() mutable {
        try {
          promise->set_value(std::move(value));
        } catch (...) {
        }
      });
    }
  };

  for (const auto& app_id : apps) {
    auto guard = std::make_shared<PromiseGuard>();
    guard->promise = std::make_shared<std::promise<ErrorOr<bool>>>();
    auto future = guard->promise->get_future();

    auto shim = std::make_shared<FlatpakShim>(nullptr, messenger, strand.get());
    shim->ApplicationStart(
        app_id, *strand, portal_manager,
        [guard](const ErrorOr<bool>& start_result) {
          spdlog::debug("[FlatpakPlugin] ApplicationStart callback received");
          guard->set_value(start_result);
        });

    auto status = future.wait_for(std::chrono::minutes(2));
    ASSERT_NE(status, std::future_status::timeout)
        << "App start timed out for " << app_id;

    auto result = future.get();
    EXPECT_TRUE(result.value()) << "Failed to start app: " << app_id;

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  std::this_thread::sleep_for(std::chrono::seconds(5));

  for (const auto& app_id : apps) {
    bool running = FlatpakShim::is_app_running(app_id);
    EXPECT_TRUE(running) << "App not running: " << app_id;
  }

  for (const auto& app_id : apps) {
    auto stop_result = FlatpakShim::ApplicationStop(app_id);
    EXPECT_TRUE(stop_result.value()) << "Failed to stop app: " << app_id;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  for (const auto& app_id : apps) {
    bool running = FlatpakShim::is_app_running(app_id);
    EXPECT_FALSE(running) << "App still running: " << app_id;
  }

  work_guard.reset();
  io_context->stop();
  if (io_thread.joinable()) {
    io_thread.join();
  }
}

TEST_F(FlatpakPluginTest, GetUpdatesTest) {
  const auto apps = FlatpakShim::GetApplicationsUpdate();

  EXPECT_GT(apps.value().size(), 0);
}

// Tested with Applications/runtimes.
TEST_F(FlatpakPluginTest, ApplicationUpdateTest) {
  auto io_context = std::make_shared<asio::io_context>();
  auto work_guard = asio::make_work_guard(*io_context);
  auto strand = std::make_shared<asio::io_context::strand>(*io_context);

  std::thread io_thread([io_context]() { io_context->run(); });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  struct PromiseGuard {
    std::shared_ptr<std::promise<ErrorOr<bool>>> promise;
    std::once_flag flag;

    void set_value(ErrorOr<bool> value) {
      std::call_once(flag, [this, value = std::move(value)]() mutable {
        try {
          promise->set_value(std::move(value));
        } catch (...) {
        }
      });
    }
  };

  auto guard = std::make_shared<PromiseGuard>();
  guard->promise = std::make_shared<std::promise<ErrorOr<bool>>>();
  auto future = guard->promise->get_future();

  auto messenger = GetTestMessenger();

  auto shim = std::make_shared<FlatpakShim>(nullptr, messenger, strand.get());

  auto app_id = "org.videolan.VLC";

  shim->ApplicationUpdate(app_id, [guard](const ErrorOr<bool>& result) {
    guard->set_value(result);
  });

  auto status = future.wait_for(std::chrono::minutes(10));

  ASSERT_NE(status, std::future_status::timeout)
      << "Update timed out after 10 minutes";

  auto result = future.get();

  ASSERT_TRUE(result.value()) << "Update failed: " << result.error().message();

  auto apps = FlatpakShim::GetApplicationsUpdate();

  bool found = false;
  for (const auto& app : apps.value()) {
    if (std::holds_alternative<flutter::CustomEncodableValue>(app)) {
      auto app_map = std::get<flutter::CustomEncodableValue>(app);
      if (app_map.type() == typeid(Application)) {
        const Application& application = std::any_cast<Application>(app_map);
        if (application.name() == "Zoom") {
          found = true;
          break;
        }
      }
    }
  }
  EXPECT_FALSE(found) << "App " << app_id << " not found in updates list";
  std::this_thread::sleep_for(std::chrono::seconds(2));

  work_guard.reset();
  io_context->stop();

  if (io_thread.joinable()) {
    io_thread.join();
  }
}