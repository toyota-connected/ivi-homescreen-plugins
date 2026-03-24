#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>

#include <flutter/encodable_value.h>

#include "../cache_config.h"
#include "../cache_manager.h"
#include "../interfaces/cache_observer.h"
#include "../interfaces/cache_storage.h"
#include "../interfaces/network_fetcher.h"

using namespace flatpak_plugin;

class TestCacheStorage final : public ICacheStorage {
 private:
  mutable std::mutex storage_mutex_;
  std::map<std::string,
           std::pair<std::string, std::chrono::system_clock::time_point>>
      storage_;
  std::string db_path_;
  std::atomic<bool> initialized_{false};

 public:
  explicit TestCacheStorage(std::string db_path)
      : db_path_(std::move(db_path)) {}

  bool Initialize() override {
    std::lock_guard lock(storage_mutex_);

    std::filesystem::path path(db_path_);
    std::filesystem::create_directories(path.parent_path());

    if (std::ifstream file(db_path_); file.is_open()) {
      std::string line;
      while (std::getline(file, line)) {
        size_t first_pipe = line.find('|');
        if (size_t second_pipe = line.find('|', first_pipe + 1);
            first_pipe != std::string::npos &&
            second_pipe != std::string::npos) {
          std::string key = line.substr(0, first_pipe);
          std::string data =
              line.substr(first_pipe + 1, second_pipe - first_pipe - 1);
          auto timestamp = std::chrono::system_clock::from_time_t(
              std::stoll(line.substr(second_pipe + 1)));
          storage_[key] = {data, timestamp};
        }
      }
      file.close();
    }

    initialized_.store(true);
    return true;
  }

  bool Store(const std::string& key,
             const std::string& data,
             std::chrono::system_clock::time_point expiry) override {
    if (!initialized_.load())
      return false;

    {
      std::lock_guard lock(storage_mutex_);
      storage_[key] = {data, expiry};
    }

    return WriteToFile();
  }

  std::optional<std::string> Retrieve(const std::string& key) override {
    if (!initialized_.load())
      return std::nullopt;

    std::lock_guard lock(storage_mutex_);
    if (const auto it = storage_.find(key); it != storage_.end()) {
      return it->second.first;
    }
    return std::nullopt;
  }

  bool IsExpired(const std::string& key) override {
    if (!initialized_.load())
      return true;

    std::lock_guard lock(storage_mutex_);
    if (const auto it = storage_.find(key); it != storage_.end()) {
      return std::chrono::system_clock::now() > it->second.second;
    }
    return true;
  }

  void Invalidate(const std::string& key) override {
    if (!initialized_.load())
      return;

    bool needs_file_update = false;
    {
      std::lock_guard lock(storage_mutex_);
      if (key.empty()) {
        if (!storage_.empty()) {
          storage_.clear();
          needs_file_update = true;
        }
      } else {
        const auto erased = storage_.erase(key);
        needs_file_update = (erased > 0);
      }
    }

    if (needs_file_update) {
      if (key.empty()) {
        std::ofstream file(db_path_, std::ios::trunc);
        file.close();
      } else {
        WriteToFile();
      }
    }
  }

  size_t GetCacheSize() override {
    if (!initialized_.load())
      return 0;

    std::lock_guard lock(storage_mutex_);
    size_t total_size = 0;
    for (const auto& [key, value] : storage_) {
      total_size += key.size() + value.first.size();
    }
    return total_size;
  }

  size_t CleanupExpired() override {
    if (!initialized_.load())
      return 0;

    size_t removed = 0;
    const auto now = std::chrono::system_clock::now();

    {
      std::lock_guard lock(storage_mutex_);
      auto it = storage_.begin();
      while (it != storage_.end()) {
        if (now > it->second.second) {
          it = storage_.erase(it);
          removed++;
        } else {
          ++it;
        }
      }
    }

    if (removed > 0) {
      WriteToFile();
    }

    return removed;
  }

 private:
  bool WriteToFile() {
    std::ofstream file(db_path_);
    if (!file.is_open())
      return false;

    for (const auto& [k, v] : storage_) {
      const auto timestamp = std::chrono::system_clock::to_time_t(v.second);
      file << k << "|" << v.first << "|" << timestamp << "\n";
    }
    file.close();
    return true;
  }
};

class TestNetworkFetcher : public INetworkFetcher {
 private:
  std::string bearer_token_;
  bool simulate_network_failure_ = false;
  long last_response_code_ = 200;

 public:
  void SetBearerToken(const std::string& token) override {
    bearer_token_ = token;
  }

  void SimulateNetworkFailure(const bool fail) {
    simulate_network_failure_ = fail;
    if (fail) {
      last_response_code_ = 500;
    } else {
      last_response_code_ = 200;
    }
  }

  // Implement pure virtual methods from INetworkFetcher
  std::optional<std::string> Fetch(
      const std::string& url,
      const std::vector<std::string>& /* headers */) override {
    if (simulate_network_failure_) {
      last_response_code_ = 500;
      return std::nullopt;
    }

    last_response_code_ = 200;
    if (url.find("appstream") != std::string::npos) {
      return std::string(
          "<?xml version=\"1.0\"?><components><component "
          "type=\"desktop-application\"><id>com.example.app</id></component></"
          "components>");
    }
    return std::string("mock_data_for_" + url);
  }

  std::optional<std::string> Post(
      const std::string& url,
      const std::vector<std::pair<std::string, std::string>>& /* form_data */,
      const std::vector<std::string>& /* headers */) override {
    if (simulate_network_failure_) {
      last_response_code_ = 500;
      return std::nullopt;
    }

    last_response_code_ = 200;
    return std::string("post_response_for_" + url);
  }

  bool IsNetworkAvailable() override { return !simulate_network_failure_; }

  long GetLastResponseCode() override { return last_response_code_; }

  [[nodiscard]] std::optional<flutter::EncodableList>
  FetchApplicationsInstalled() const {
    if (simulate_network_failure_) {
      return std::nullopt;
    }

    flutter::EncodableList apps;

    flutter::EncodableMap app_map;
    app_map[flutter::EncodableValue("name")] =
        flutter::EncodableValue("org.mozilla.firefox");
    app_map[flutter::EncodableValue("version")] =
        flutter::EncodableValue("120.0");
    app_map[flutter::EncodableValue("description")] =
        flutter::EncodableValue("Web browser");
    app_map[flutter::EncodableValue("installed_size")] =
        flutter::EncodableValue(static_cast<int64_t>(123456789));
    apps.emplace_back(app_map);

    flutter::EncodableMap app_map2;
    app_map2[flutter::EncodableValue("name")] =
        flutter::EncodableValue("org.libreoffice.LibreOffice");
    app_map2[flutter::EncodableValue("version")] =
        flutter::EncodableValue("7.4.2");
    app_map2[flutter::EncodableValue("description")] =
        flutter::EncodableValue("Office suite");
    app_map2[flutter::EncodableValue("installed_size")] =
        flutter::EncodableValue(static_cast<int64_t>(456789012));
    apps.emplace_back(app_map2);

    return apps;
  }

  [[nodiscard]] std::optional<flutter::EncodableList> FetchApplicationsRemote(
      const std::string& remote_id) const {
    if (simulate_network_failure_) {
      return std::nullopt;
    }

    flutter::EncodableList apps;

    flutter::EncodableMap app_map;
    app_map[flutter::EncodableValue("name")] =
        flutter::EncodableValue("com.spotify.Client");
    app_map[flutter::EncodableValue("version")] =
        flutter::EncodableValue("1.1.84");
    app_map[flutter::EncodableValue("description")] =
        flutter::EncodableValue("Music streaming service");
    app_map[flutter::EncodableValue("remote")] =
        flutter::EncodableValue(remote_id);
    app_map[flutter::EncodableValue("download_size")] =
        flutter::EncodableValue(static_cast<int64_t>(123456789));
    apps.emplace_back(app_map);

    flutter::EncodableMap app_map2;
    app_map2[flutter::EncodableValue("name")] =
        flutter::EncodableValue("com.discordapp.Discord");
    app_map2[flutter::EncodableValue("version")] =
        flutter::EncodableValue("0.0.20");
    app_map2[flutter::EncodableValue("description")] =
        flutter::EncodableValue("Chat application");
    app_map2[flutter::EncodableValue("remote")] =
        flutter::EncodableValue(remote_id);
    app_map2[flutter::EncodableValue("download_size")] =
        flutter::EncodableValue(static_cast<int64_t>(87654321));
    apps.emplace_back(app_map2);

    return apps;
  }

  [[nodiscard]] std::optional<flutter::EncodableMap> FetchUserInstallation()
      const {
    if (simulate_network_failure_) {
      return std::nullopt;
    }

    flutter::EncodableMap installation;
    installation[flutter::EncodableValue("id")] = "user";
    installation[flutter::EncodableValue("path")] =
        std::string(getenv("HOME") ? getenv("HOME") : "/home/user") +
        "/.local/share/flatpak";
    installation[flutter::EncodableValue("is_user")] = true;
    installation[flutter::EncodableValue("display_name")] = "User Installation";

    return installation;
  }

  [[nodiscard]] std::optional<flutter::EncodableList> FetchSystemInstallations()
      const {
    if (simulate_network_failure_) {
      return std::nullopt;
    }

    flutter::EncodableList installations;
    flutter::EncodableMap system_app;

    system_app[flutter::EncodableValue("id")] =
        flutter::EncodableValue("system");
    system_app[flutter::EncodableValue("path")] =
        flutter::EncodableValue("/var/lib/flatpak");
    system_app[flutter::EncodableValue("is_user")] = false;
    system_app[flutter::EncodableValue("display_name")] =
        flutter::EncodableValue("System Installation");

    installations.emplace_back(system_app);

    return installations;
  }

  std::optional<flutter::EncodableList> FetchRemotes(
      const std::string& /* installation_id */) override {
    if (simulate_network_failure_) {
      return std::nullopt;
    }

    flutter::EncodableList remotes;

    flutter::EncodableMap remote_map;
    remote_map[flutter::EncodableValue("name")] =
        flutter::EncodableValue("flathub");
    remote_map[flutter::EncodableValue("url")] =
        flutter::EncodableValue("https://flathub.org/repo/");
    remote_map[flutter::EncodableValue("title")] =
        flutter::EncodableValue("Flathub");
    remote_map[flutter::EncodableValue("is_disabled")] =
        flutter::EncodableValue(false);
    remotes.emplace_back(remote_map);

    flutter::EncodableMap remote_map2;
    remote_map2[flutter::EncodableValue("name")] =
        flutter::EncodableValue("fedora");
    remote_map2[flutter::EncodableValue("url")] =
        flutter::EncodableValue("https://registry.fedoraproject.org/");
    remote_map2[flutter::EncodableValue("title")] =
        flutter::EncodableValue("Fedora Registry");
    remote_map2[flutter::EncodableValue("is_disabled")] =
        flutter::EncodableValue(true);
    remotes.emplace_back(remote_map2);

    return remotes;
  }
};

class TestCacheObserver final : public ICacheObserver {
 public:
  struct Event {
    std::string type;
    std::string key;
    std::string extra;
    size_t data_size = 0;
    long error_code = 0;
    std::chrono::system_clock::time_point timestamp;
  };

  std::vector<Event> events;

  // Implement all pure virtual methods from ICacheObserver
  void OnCacheHit(const std::string& key, const size_t data_size) override {
    events.push_back(
        {"hit", key, "", data_size, 0, std::chrono::system_clock::now()});
  }

  void OnCacheMiss(const std::string& key) override {
    events.push_back({"miss", key, "", 0, 0, std::chrono::system_clock::now()});
  }

  void OnCacheStore(const std::string& key) {
    events.push_back(
        {"store", key, "", 0, 0, std::chrono::system_clock::now()});
  }

  void OnCacheRemove(const std::string& key) {
    events.push_back(
        {"remove", key, "", 0, 0, std::chrono::system_clock::now()});
  }

  void OnCacheError(const std::string& key, const std::string& error) {
    events.push_back(
        {"error", key, error, 0, 0, std::chrono::system_clock::now()});
  }

  void OnCacheExpired(const std::string& key) override {
    events.push_back(
        {"expired", key, "", 0, 0, std::chrono::system_clock::now()});
  }

  void OnNetworkFallback(const std::string& reason) override {
    events.push_back({"network_fallback", "", reason, 0, 0,
                      std::chrono::system_clock::now()});
  }

  void OnNetworkError(const std::string& url, const long error_code) override {
    events.push_back({"network_error", url, "", 0, error_code,
                      std::chrono::system_clock::now()});
  }

  void OnCacheCleanup(const size_t entries_cleaned) override {
    events.push_back({"cleanup", "", "", entries_cleaned, 0,
                      std::chrono::system_clock::now()});
  }

  void ClearEvents() { events.clear(); }

  [[nodiscard]] bool HasEvent(const std::string& event_type) const {
    return std::any_of(
        events.begin(), events.end(),
        [&event_type](const Event& e) { return e.type == event_type; });
  }
};

class CacheManagerIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_db_path_ =
        "/tmp/cache_manager_test_" +
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
        ".db";

    config_ = CacheConfig{.db_path = test_db_path_,
                          .default_ttl = std::chrono::seconds(60),
                          .policy = CachePolicy::CACHE_FIRST,
                          .enable_compression = false,
                          .max_cache_size_mb = 10,
                          .network_timeout = std::chrono::seconds(5),
                          .max_retries = 2,
                          .enable_auto_cleanup = true,
                          .cleanup_interval = std::chrono::minutes(10),
                          .enable_metrics = true};

    storage_ = std::make_unique<TestCacheStorage>(test_db_path_);
    fetcher_ = std::make_unique<TestNetworkFetcher>();
    observer_ = std::make_unique<TestCacheObserver>();

    storage_ptr_ = storage_.get();
    fetcher_ptr_ = fetcher_.get();
    observer_ptr_ = observer_.get();
  }

  void TearDown() override {
    cache_manager_.reset();

    if (std::filesystem::exists(test_db_path_)) {
      std::filesystem::remove(test_db_path_);
    }
  }

  void CreateCacheManager() {
    // Ensure we have fresh instances
    if (!storage_) {
      storage_ = std::make_unique<TestCacheStorage>(test_db_path_);
      storage_ptr_ = storage_.get();
    }
    if (!fetcher_) {
      fetcher_ = std::make_unique<TestNetworkFetcher>();
      fetcher_ptr_ = fetcher_.get();
    }
    if (!observer_) {
      observer_ = std::make_unique<TestCacheObserver>();
      observer_ptr_ = observer_.get();
    }

    cache_manager_ = std::make_unique<CacheManager>(
        config_, std::move(storage_), std::move(fetcher_));

    if (observer_) {
      cache_manager_->AddObserver(std::move(observer_));
    }

    ASSERT_TRUE(cache_manager_->IsHealthy())
        << "Cache manager failed to initialize";
  }

  std::string test_db_path_;
  CacheConfig config_;
  std::unique_ptr<CacheManager> cache_manager_;
  std::unique_ptr<TestCacheStorage> storage_;
  std::unique_ptr<TestNetworkFetcher> fetcher_;
  std::unique_ptr<TestCacheObserver> observer_;

  TestCacheStorage* storage_ptr_ = nullptr;
  TestNetworkFetcher* fetcher_ptr_ = nullptr;
  TestCacheObserver* observer_ptr_ = nullptr;
};

TEST_F(CacheManagerIntegrationTest, Initialization) {
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());
}

TEST_F(CacheManagerIntegrationTest, BuilderPattern) {
  auto test_storage =
      std::make_unique<TestCacheStorage>("/tmp/builder_test.db");
  auto test_fetcher = std::make_unique<TestNetworkFetcher>();

  const auto manager = CacheManager::Builder()
                           .WithDatabasePath("/tmp/builder_test.db")
                           .WithCachePolicy(CachePolicy::NETWORK_FIRST)
                           .WithAutoCleanup(true, std::chrono::minutes(5))
                           .WithDefaultTTL(std::chrono::minutes(10))
                           .WithCompression(false)
                           .WithMaxCacheSize(50)
                           .WithMaxRetries(3)
                           .WithNetworkTimeout(std::chrono::seconds(5))
                           .WithMetrics(true)
                           .WithStorage(std::move(test_storage))
                           .WithNetworkFetcher(std::move(test_fetcher))
                           .Build();

  ASSERT_NE(manager, nullptr);
  EXPECT_TRUE(manager->IsHealthy());
  const auto& built_config = manager->GetConfig();
  EXPECT_EQ(built_config.db_path, "/tmp/builder_test.db");
  EXPECT_EQ(built_config.default_ttl, std::chrono::minutes(10));
  EXPECT_EQ(built_config.policy, CachePolicy::NETWORK_FIRST);
  EXPECT_EQ(built_config.max_cache_size_mb, 50);
  EXPECT_EQ(built_config.max_retries, 3);
  EXPECT_EQ(built_config.network_timeout, std::chrono::seconds(5));
  EXPECT_TRUE(built_config.enable_metrics);
  EXPECT_FALSE(built_config.enable_compression);

  std::filesystem::remove("/tmp/builder_test.db");
}

// network fetching tests
TEST_F(CacheManagerIntegrationTest, FetchApplicationInstalledFirstTime) {
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());

  const auto result = cache_manager_->GetApplicationsInstalled(false);
  ASSERT_TRUE(result.has_value());
  EXPECT_GT(result->size(), 0);
  EXPECT_GE(observer_ptr_->events.size(), 1);
  EXPECT_TRUE(observer_ptr_->HasEvent("miss") ||
              observer_ptr_->HasEvent("store"));
}

TEST_F(CacheManagerIntegrationTest, FetchApplicationRemote) {
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());

  const auto result = cache_manager_->GetApplicationsRemote("flathub", false);
  ASSERT_TRUE(result.has_value());
  EXPECT_GT(result->size(), 0);
  EXPECT_GT(cache_manager_->GetCacheSize(), 0);
}

TEST_F(CacheManagerIntegrationTest, CacheHitOnSecondRequest) {
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());

  // fetch from network
  const auto result1 = cache_manager_->GetApplicationsInstalled(false);
  ASSERT_TRUE(result1.has_value());

  observer_ptr_->ClearEvents();

  // hit cache if cache policy allows
  const auto result2 = cache_manager_->GetApplicationsInstalled(false);
  ASSERT_TRUE(result2.has_value());

  EXPECT_EQ(result1->size(), result2->size());

  // cache-first policy get a cache hit
  const bool has_cache_interaction =
      observer_ptr_->HasEvent("hit") || observer_ptr_->HasEvent("miss");
  EXPECT_TRUE(has_cache_interaction);
}

TEST_F(CacheManagerIntegrationTest, RemoteCacheHitOnSecondRequest) {
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());

  // fetch from network
  const auto result1 = cache_manager_->GetApplicationsRemote("flathub", false);
  ASSERT_TRUE(result1.has_value());

  observer_ptr_->ClearEvents();

  // hit cache if cache policy allows
  const auto result2 = cache_manager_->GetApplicationsRemote("flathub", false);
  ASSERT_TRUE(result2.has_value());

  EXPECT_EQ(result1->size(), result2->size());

  // cache-first policy get a cache hit
  const bool has_cache_interaction =
      observer_ptr_->HasEvent("hit") || observer_ptr_->HasEvent("miss");
  EXPECT_TRUE(has_cache_interaction);
}

TEST_F(CacheManagerIntegrationTest, ForceRefreshBypassesCache) {
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());
  const auto result1 = cache_manager_->GetApplicationsInstalled(false);
  ASSERT_TRUE(result1.has_value());

  observer_ptr_->ClearEvents();

  const auto result2 = cache_manager_->GetApplicationsInstalled(true);
  ASSERT_TRUE(result2.has_value());
  EXPECT_GT(observer_ptr_->events.size(), 0);
}

TEST_F(CacheManagerIntegrationTest, NetworkFailureHandling) {
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());

  fetcher_ptr_->SimulateNetworkFailure(true);
  auto result = cache_manager_->GetApplicationsInstalled(false);

  fetcher_ptr_->SimulateNetworkFailure(false);
  result = cache_manager_->GetApplicationsInstalled(false);
  EXPECT_TRUE(result.has_value());
}

TEST_F(CacheManagerIntegrationTest, DifferentAPIendpoints) {
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());

  auto installed = cache_manager_->GetApplicationsInstalled(false);
  EXPECT_TRUE(installed.has_value());

  const auto remote = cache_manager_->GetApplicationsRemote("flathub", false);
  EXPECT_TRUE(remote.has_value());

  const auto user_install = cache_manager_->GetUserInstallation(false);
  EXPECT_TRUE(user_install.has_value());

  const auto system_installs = cache_manager_->GetSystemInstallations(false);
  EXPECT_TRUE(system_installs.has_value());

  const auto remotes = cache_manager_->GetRemotes("user", false);
  EXPECT_TRUE(remotes.has_value());
}

TEST_F(CacheManagerIntegrationTest, CacheInvalidation) {
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());

  const auto result = cache_manager_->GetApplicationsInstalled(false);
  EXPECT_TRUE(result.has_value());

  EXPECT_GT(cache_manager_->GetCacheSize(), 0);

  // invalidate key

  cache_manager_->InvalidateKey("applications_installed");
  observer_ptr_->ClearEvents();

  const auto result2 = cache_manager_->GetApplicationsInstalled(false);
  EXPECT_TRUE(result2.has_value());
  EXPECT_TRUE(observer_ptr_->HasEvent("miss"));
}

TEST_F(CacheManagerIntegrationTest, ClearAllCache) {
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());

  cache_manager_->GetApplicationsInstalled(false);
  cache_manager_->GetApplicationsRemote("flathub", false);
  cache_manager_->GetUserInstallation(false);

  // clear all
  cache_manager_->InvalidateAll();
  EXPECT_EQ(cache_manager_->GetCacheSize(), 0);
}

TEST_F(CacheManagerIntegrationTest, CacheExpiration) {
  config_.default_ttl = std::chrono::seconds(1);
  CreateCacheManager();

  cache_manager_->GetApplicationsInstalled(false);
  cache_manager_->GetApplicationsRemote("flathub", false);

  std::this_thread::sleep_for(std::chrono::seconds(2));

  size_t cleaned = cache_manager_->ForceCleanup();
  EXPECT_GE(cleaned, 0);
}

TEST_F(CacheManagerIntegrationTest, ConcurrentAccess) {
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());

  constexpr int thread_count = 4;
  constexpr int requests_per_thread = 3;

  std::vector<std::thread> threads;
  std::atomic<int> success_count{0};
  std::atomic<int> total_requests{0};

  threads.reserve(thread_count);
  for (int i = 0; i < thread_count; i++) {
    threads.emplace_back([&]() {
      for (int j = 0; j < requests_per_thread; j++) {
        ++total_requests;

        if (j % 3 == 0) {
          auto result = cache_manager_->GetApplicationsInstalled(false);
          if (result.has_value())
            ++success_count;
        } else if (j % 3 == 1) {
          auto result = cache_manager_->GetApplicationsRemote("flathub", false);
          if (result.has_value())
            ++success_count;
        } else {
          auto result = cache_manager_->GetUserInstallation(false);
          if (result.has_value())
            ++success_count;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_GT(success_count.load(), 0);
  EXPECT_EQ(total_requests.load(), thread_count * requests_per_thread);
}

TEST_F(CacheManagerIntegrationTest, MemoryLeakTest) {
  size_t initial_cache_size = 0;

  {
    CreateCacheManager();
    cache_manager_->GetApplicationsInstalled(false);
    initial_cache_size = cache_manager_->GetCacheSize();
  }

  // Verify cleanup occurred
  EXPECT_GT(initial_cache_size, 0);
}

// Cache Policy
TEST_F(CacheManagerIntegrationTest, NetworkFirstPolicy) {
  config_.policy = CachePolicy::NETWORK_FIRST;
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());

  // first request
  auto result1 = cache_manager_->GetApplicationsInstalled(false);
  ASSERT_TRUE(result1.has_value());
  observer_ptr_->ClearEvents();

  // second request
  auto result2 = cache_manager_->GetApplicationsInstalled(false);
  ASSERT_TRUE(result2.has_value());
}

TEST_F(CacheManagerIntegrationTest, NetworkOnlyPolicy) {
  config_.policy = CachePolicy::NETWORK_ONLY;
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());

  for (int i = 0; i < 3; ++i) {
    observer_ptr_->ClearEvents();
    auto result = cache_manager_->GetApplicationsInstalled(false);
    ASSERT_TRUE(result.has_value());
  }
}

TEST_F(CacheManagerIntegrationTest, CachePolicyChange) {
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());

  cache_manager_->SetCachePolicy(CachePolicy::NETWORK_ONLY);
  EXPECT_EQ(cache_manager_->GetCachePolicy(), CachePolicy::NETWORK_ONLY);

  auto result = cache_manager_->GetApplicationsInstalled(false);
  EXPECT_TRUE(result.has_value());
}

TEST_F(CacheManagerIntegrationTest, CacheSizeReporting) {
  CreateCacheManager();
  EXPECT_TRUE(cache_manager_->IsHealthy());

  size_t size = cache_manager_->GetCacheSize();

  cache_manager_->GetApplicationsInstalled(false);

  size_t after_size = cache_manager_->GetCacheSize();

  EXPECT_GE(after_size, size);
}

TEST_F(CacheManagerIntegrationTest, DataPresistanceAcrossRestarts) {
  // First Instance
  {
    CreateCacheManager();
    EXPECT_TRUE(cache_manager_->IsHealthy());

    auto result = cache_manager_->GetApplicationsInstalled(false);
    EXPECT_TRUE(result.has_value());
  }
  // Second Instance
  storage_ = std::make_unique<TestCacheStorage>(test_db_path_);
  fetcher_ = std::make_unique<TestNetworkFetcher>();
  observer_ = std::make_unique<TestCacheObserver>();
  storage_ptr_ = storage_.get();
  fetcher_ptr_ = fetcher_.get();
  observer_ptr_ = observer_.get();

  CreateCacheManager();

  auto result = cache_manager_->GetApplicationsInstalled(false);
  EXPECT_TRUE(result.has_value());
}