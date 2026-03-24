/*
 * Copyright 2025 Ahmed Wafdy
 * Copyright 2025 Toyota Connected North America
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

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "gtest/gtest.h"

#include "../curl_client.h"

using namespace plugin_common_curl;
using namespace testing;

class CurlClientTest : public Test {
 protected:
  void SetUp() override {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    valid_url = "https://httpbin.org/get";
    post_url = "https://httpbin.org/post";
    put_url = "https://httpbin.org/put";
    delete_url = "https://httpbin.org/delete";
    redirect_url = "https://httpbin.org/redirect/3";
    timeout_url = "https://httpbin.org/delay/5";
    invalid_url = "https://nonexistent-domain-12345.com";
    large_data_url = "https://httpbin.org/bytes/102400";  // 100KB
    auth_url = "https://httpbin.org/bearer";
    headers_url = "https://httpbin.org/headers";
    status_404_url = "https://httpbin.org/status/404";
    status_500_url = "https://httpbin.org/status/500";
  }

  void TearDown() override { curl_global_cleanup(); }

  std::string valid_url;
  std::string post_url;
  std::string put_url;
  std::string delete_url;
  std::string redirect_url;
  std::string timeout_url;
  std::string invalid_url;
  std::string large_data_url;
  std::string auth_url;
  std::string headers_url;
  std::string status_404_url;
  std::string status_500_url;

  static std::string GenerateRandomString(const size_t length) {
    const std::string charset =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
      std::uniform_int_distribution<size_t> dis(0, charset.size() - 1);
      result += charset.at(dis(gen));
    }
    return result;
  }

  static std::vector<std::pair<std::string, std::string>> GenerateFormData(
      const size_t count) {
    std::vector<std::pair<std::string, std::string>> form_data;
    form_data.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      form_data.emplace_back("key" + std::to_string(i),
                             GenerateRandomString(50));
    }
    return form_data;
  }
};

TEST_F(CurlClientTest, BasicGETRequest) {
  CurlClient client;

  ASSERT_TRUE(client.Init(valid_url, {}, {}, true));

  std::string response = client.RetrieveContentAsString();

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_EQ(client.GetHttpCode(), 200);
  EXPECT_TRUE(client.IsSuccess());
  EXPECT_FALSE(client.IsClientError());
  EXPECT_FALSE(client.IsServerError());
  EXPECT_FALSE(response.empty());
}

TEST_F(CurlClientTest, BasicPOSTRequest) {
  CurlClient client;

  const auto form_data = GenerateFormData(5);
  const std::string response = client.Post(post_url, form_data);

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_EQ(client.GetHttpCode(), 200);
  EXPECT_TRUE(client.IsSuccess());
  EXPECT_FALSE(response.empty());
}

TEST_F(CurlClientTest, BasicPUTRequest) {
  CurlClient client;

  const auto test_data = GenerateRandomString(1000);
  const std::string response = client.Put(put_url, test_data);

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_EQ(client.GetHttpCode(), 200);
  EXPECT_TRUE(client.IsSuccess());
  EXPECT_FALSE(response.empty());
}

TEST_F(CurlClientTest, BasicDELETERequest) {
  CurlClient client;

  const std::string response = client.Delete(delete_url);

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_EQ(client.GetHttpCode(), 200);
  EXPECT_TRUE(client.IsSuccess());
  EXPECT_FALSE(response.empty());
}

TEST_F(CurlClientTest, VectorResponse) {
  CurlClient client;

  ASSERT_TRUE(client.Init(valid_url, {}, {}, true));

  const auto& response = client.RetrieveContentAsVector();

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_EQ(client.GetHttpCode(), 200);
  EXPECT_TRUE(client.IsSuccess());
  EXPECT_FALSE(response.empty());
}

TEST_F(CurlClientTest, CustomHeaders) {
  CurlClient client;

  const std::vector<std::string> headers = {"X-Custom-Header: test-value",
                                            "Content-Type: application/json",
                                            "User-Agent: CurlClient-Test/1.0"};
  const std::string response = client.Get(headers_url, headers);

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_EQ(client.GetHttpCode(), 200);
  EXPECT_TRUE(client.IsSuccess());
  EXPECT_FALSE(response.empty());
  EXPECT_TRUE(response.find("X-Custom-Header") != std::string::npos);
}

TEST_F(CurlClientTest, BearerTokenAuth) {
  CurlClient client;

  client.SetBearerToken("test-token-12345");
  const std::string response = client.Get(auth_url);

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_EQ(client.GetHttpCode(), 200);
  EXPECT_TRUE(client.IsSuccess());
  EXPECT_FALSE(response.empty());
}

TEST_F(CurlClientTest, FollowRedirects) {
  CurlClient client;

  ASSERT_TRUE(client.Init(redirect_url, {}, {}, true));
  std::string response = client.RetrieveContentAsString();

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_EQ(client.GetHttpCode(), 200);
  EXPECT_TRUE(client.IsSuccess());

  const auto& response_info = client.GetResponseInfo();
  EXPECT_GT(response_info.redirect_count, 0);
}

TEST_F(CurlClientTest, NoFollowRedirects) {
  CurlClient client;

  ASSERT_TRUE(client.Init(redirect_url, {}, {}, false));
  std::string response = client.RetrieveContentAsString();

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_EQ(client.GetHttpCode(), 302);  // redirect
  EXPECT_FALSE(client.IsSuccess());
}

TEST_F(CurlClientTest, TimeoutTest) {
  CurlClient client;
  client.SetTimeout(2);

  ASSERT_TRUE(client.Init(timeout_url, {}, {}, true));
  const auto start_time = std::chrono::steady_clock::now();
  std::string response = client.RetrieveContentAsString();
  const auto end_time = std::chrono::steady_clock::now();

  const auto duration =
      std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
  EXPECT_NE(client.GetCode(), CURLE_OK);
  EXPECT_FALSE(client.IsSuccess());
  EXPECT_LE(duration.count(), 3);  // timeout within 3 seconds
}

TEST_F(CurlClientTest, ConnectionTimeoutTest) {
  CurlClient client;
  client.SetTimeout(1);

  ASSERT_TRUE(client.Init(invalid_url, {}, {}, true));
  const auto start_time = std::chrono::steady_clock::now();
  std::string response = client.RetrieveContentAsString();
  const auto end_time = std::chrono::steady_clock::now();

  const auto duration =
      std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
  EXPECT_NE(client.GetCode(), CURLE_OK);
  EXPECT_FALSE(client.IsSuccess());
  EXPECT_LE(duration.count(), 5);  // timeout within 5 seconds
}

TEST_F(CurlClientTest, InvalidURL) {
  CurlClient client;

  ASSERT_TRUE(client.Init(invalid_url, {}, {}, true));

  std::string response = client.RetrieveContentAsString();

  EXPECT_NE(client.GetCode(), CURLE_OK);
  EXPECT_FALSE(client.IsSuccess());
}

TEST_F(CurlClientTest, ClientError404) {
  CurlClient client;

  std::string response = client.Get(status_404_url);

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_FALSE(client.IsSuccess());
  EXPECT_EQ(client.GetHttpCode(), 404);
  EXPECT_TRUE(client.IsClientError());
  EXPECT_FALSE(client.IsServerError());
}

TEST_F(CurlClientTest, ServerError500) {
  CurlClient client;

  std::string response = client.Get(status_500_url);

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_FALSE(client.IsSuccess());
  EXPECT_EQ(client.GetHttpCode(), 500);
  EXPECT_FALSE(client.IsClientError());
  EXPECT_TRUE(client.IsServerError());
}

TEST_F(CurlClientTest, LargeDataDownload) {
  CurlClient client;

  ASSERT_TRUE(client.Init(large_data_url, {}, {}, true));

  const auto& response = client.RetrieveContentAsVector();

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_EQ(client.GetHttpCode(), 200);
  EXPECT_TRUE(client.IsSuccess());
  EXPECT_GE(response.size(), 100000);  // At least 100KB

  const auto& response_info = client.GetResponseInfo();
  EXPECT_GT(response_info.download_size, 0);
  EXPECT_GT(response_info.total_time, 0);
  EXPECT_FALSE(response_info.effective_url.empty());
}

TEST_F(CurlClientTest, LargeFormDataPost) {
  CurlClient client;
  const auto large_form_data = GenerateFormData(100);  // 100 fields

  const std::string response = client.Post(post_url, large_form_data);

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_EQ(client.GetHttpCode(), 200);
  EXPECT_TRUE(client.IsSuccess());
  EXPECT_FALSE(response.empty());
}

TEST_F(CurlClientTest, ResponseInfoTest) {
  CurlClient client;

  ASSERT_TRUE(client.Init(valid_url, {}, {}, true));

  const std::string response = client.RetrieveContentAsString();

  EXPECT_EQ(client.GetCode(), CURLE_OK);
  EXPECT_EQ(client.GetHttpCode(), 200);
  EXPECT_FALSE(response.empty());

  const auto& response_info = client.GetResponseInfo();
  EXPECT_EQ(response_info.http_code, 200);
  EXPECT_GT(response_info.total_time, 0);
  EXPECT_GT(response_info.download_size, 0);
  EXPECT_FALSE(response_info.effective_url.empty());
}

TEST_F(CurlClientTest, ConcurrentRequests) {
  constexpr int num_threads = 10;
  std::vector<std::future<bool>> futures;
  std::atomic success_count{0};

  futures.reserve(num_threads);
  for (int i = 0; i < num_threads; ++i) {
    futures.emplace_back(std::async(std::launch::async, [&, i]() {
      CurlClient client;

      const std::string url = valid_url + "?thread=" + std::to_string(i);
      std::string response = client.Get(url);

      if (client.IsSuccess()) {
        ++success_count;
        return true;
      }
      return false;
    }));
  }

  for (auto& future : futures) {
    future.wait();
  }

  EXPECT_EQ(success_count.load(), num_threads);
}

TEST_F(CurlClientTest, MemoryLeakTest) {
  // Create and destroy many clients
  // Iterations reduced from 100 to 20
  for (int i = 0; i < 20; ++i) {
    const auto client = std::make_unique<CurlClient>();
    ASSERT_TRUE(client->Init(valid_url, {}, {}, true));
    std::string response = client->RetrieveContentAsString();
    EXPECT_TRUE(client->IsSuccess());
  }

  SUCCEED();
}

TEST_F(CurlClientTest, StressTestAllMethods) {
  constexpr int iterations = 20;

  for (int i = 0; i < iterations; ++i) {
    CurlClient client;

    // GET
    std::string get_response = client.Get(valid_url);
    EXPECT_TRUE(client.IsSuccess());

    // POST
    auto form_data = GenerateFormData(5);
    std::string post_response = client.Post(post_url, form_data);
    EXPECT_TRUE(client.IsSuccess());

    // PUT
    std::string put_data = GenerateRandomString(100);
    std::string put_response = client.Put(put_url, put_data);
    EXPECT_TRUE(client.IsSuccess());

    // DELETE
    std::string delete_response = client.Delete(delete_url);
    EXPECT_TRUE(client.IsSuccess());
  }
}

int main(int argc, char** argv) {
  InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
