/*
 * Copyright 2023-2025 Toyota Connected North America
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

#ifndef PLUGINS_COMMON_CURL_CLIENT_CURL_CLIENT_H_
#define PLUGINS_COMMON_CURL_CLIENT_CURL_CLIENT_H_

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <curl/curl.h>

namespace plugin_common_curl {

struct ResponseInfo {
  long http_code = 0;
  double total_time = 0.0;
#if LIBCURL_VERSION_NUM >= 0x073700  // 7.55.0
  curl_off_t download_size = 0;
  curl_off_t upload_size = 0;
#else
  double download_size = 0.0;
  double upload_size = 0.0;
#endif
  long redirect_count = 0;
  std::string effective_url;
  std::string content_type;
  std::map<std::string, std::string> headers;
};

class CurlClient {
 public:
  CurlClient();
  ~CurlClient();

  /**
   * @brief Initialization Function
   * @param url The url to use
   * @param headers vector of headers to use
   * @param url_form url form key/values
   * @param follow_location follows redirects from server.  Defaults to true
   * @param verbose flag to enable stderr output of curl dialog
   * @return bool
   * @retval true if initialized, false if failed
   * @relation
   * google_sign_in
   */
  bool Init(const std::string& url,
            const std::vector<std::string>& headers,
            const std::vector<std::pair<std::string, std::string>>& url_form,
            bool follow_location = true,
            bool verbose = false);

  /**
   * @brief Function to execute http client
   * @param verbose flag to enable stderr output of curl dialog
   * @return std::string
   * @retval body response of client request
   * @relation
   * google_sign_in
   */
  std::string RetrieveContentAsString(bool verbose = false);

  /**
   * @brief Function to execute http client
   * @param verbose flag to enable stderr output of curl dialog
   * @return const std::vector<uint8_t>&
   * @retval body response of client request
   * @relation
   * google_sign_in
   */
  const std::vector<uint8_t>& RetrieveContentAsVector(bool verbose = false);

  /**
   * @brief Function to return last curl response code
   * @return CURLcode
   * @retval the last response code.  Used to check response state
   * @relation
   * google_sign_in
   */
  [[nodiscard]] CURLcode GetCode() const { return mCode; }

  /**
   * @brief Function to return HTTP response code
   * @return long
   * @retval the HTTP response code (200, 404, 500, etc.)
   * @relation
   * google_sign_in
   */
  [[nodiscard]] long GetHttpCode() const { return mResponseInfo.http_code; }

  /**
   * @brief Function to return complete response information
   * @return const ResponseInfo&
   * @retval detailed response information including timing, sizes, etc.
   * @relation
   * google_sign_in
   */
  [[nodiscard]] const ResponseInfo& GetResponseInfo() const {
    return mResponseInfo;
  }

  /**
   * @brief Function to check if request was successful
   * @return bool
   * @retval true if CURL operation succeeded AND HTTP code indicates success
   * @relation
   * google_sign_in
   */
  [[nodiscard]] bool IsSuccess() const {
    return mCode == CURLE_OK && mResponseInfo.http_code >= 200 &&
           mResponseInfo.http_code < 300;
  }
  /**
   * @brief Function to check if request had client error (4xx)
   * @return bool
   * @retval true if HTTP code is 4xx
   * @relation
   * google_sign_in
   */
  [[nodiscard]] bool IsClientError() const {
    return mResponseInfo.http_code >= 400 && mResponseInfo.http_code < 500;
  }

  /**
   * @brief Function to check if request had server error (5xx)
   * @return bool
   * @retval true if HTTP code is 5xx
   * @relation
   * google_sign_in
   */
  [[nodiscard]] bool IsServerError() const {
    return mResponseInfo.http_code >= 500 && mResponseInfo.http_code < 600;
  }

  /**
   * @brief Set request timeout in seconds
   * @param timeout_seconds Timeout in seconds
   * @relation
   * google_sign_in
   */
  void SetTimeout(long timeout_seconds);

  /**
   * @brief Set connection timeout in seconds
   * @param timeout_seconds Connection timeout in seconds
   * @relation
   * google_sign_in
   */
  void SetConnectionTimeout(long timeout_seconds);

  /**
   * @brief Set maximum number of redirects to follow
   * @param max_redirects Maximum redirects (-1 for unlimited)
   * @relation
   * google_sign_in
   */
  void SetMaxRedirects(long max_redirects);

  /**
   * @brief Function sets the bearer token for the OAuth 2.0 authentication.
   * @param token The authentication token.
   * @relation
   * google_sign_in
   */
  void SetBearerToken(const std::string& token);

  /**
   * @brief Performs an HTTP POST request with URL-encoded from data.
   * @param url The URL to post to.
   * @param additional_headers A vector of additional headers to send.
   * @return std::string The response body.
   */
  std::string Get(const std::string& url,
                  const std::vector<std::string>& additional_headers = {});

  /**
   * @brief Performs an HTTP POST request with URL-encoded from data.
   * @param url The URL to post to.
   * @param form_data A vector of key-valued pair for the form data.
   * @param additional_headers A vector of additional headers to send.
   * @return std::string The response body.
   */
  std::string Post(
      const std::string& url,
      const std::vector<std::pair<std::string, std::string>>& form_data,
      const std::vector<std::string>& additional_headers = {});

  /**
   * @brief Performs an HTTP PUT request
   * @param url The URL to put to
   * @param data Data to put
   * @param additional_headers A vector of additional headers to send
   * @return std::string The response body
   */
  std::string Put(const std::string& url,
                  const std::string& data,
                  const std::vector<std::string>& additional_headers = {});

  /**
   * @brief Performs an HTTP DELETE request
   * @param url The URL to delete
   * @param additional_headers A vector of additional headers to send
   * @return std::string The response body
   */
  std::string Delete(const std::string& url,
                     const std::vector<std::string>& additional_headers = {});

  // Prevent copying.
  CurlClient(CurlClient const&) = delete;
  CurlClient& operator=(CurlClient const&) = delete;

 private:
  CURL* mConn{};
  struct curl_slist* mHeadersList{};
  CURLcode mCode;
  std::string mUrl;
  std::string mAuthHeader;
  std::string mPostFields;
  std::unique_ptr<char[]> mErrorBuffer;
  std::string mStringBuffer;
  std::vector<uint8_t> mVectorBuffer;
  ResponseInfo mResponseInfo;
  std::string mHeaderBuffer;

  long mTimeout{30};
  long mConnectionTimeout{10};
  long mMaxRedirects{5};

  /**
   * @brief Callback function for curl client
   * @param data buffer of response
   * @param size length of buffer
   * @param num_mem_block number of memory blocks
   * @param writerData user pointer
   * @return int
   * @retval returns back to curl size of write
   * @relation
   * google_sign_in
   */
  static int StringWriter(const char* data,
                          size_t size,
                          size_t num_mem_block,
                          std::string* writerData);

  /**
   * @brief Callback function for curl client
   * @param data buffer of response
   * @param size length of buffer
   * @param num_mem_block number of memory blocks
   * @param writerData user pointer
   * @return int
   * @retval returns back to curl size of write
   * @relation
   * google_sign_in
   */
  static int VectorWriter(char* data,
                          size_t size,
                          size_t num_mem_block,
                          std::vector<uint8_t>* writerData);

  /**
   * @brief Callback function for curl client header content
   */
  static size_t HeaderWriter(char* data,
                             size_t size,
                             size_t num_mem_block,
                             std::string* writerData);

  /**
   * @brief Internal function to setup common curl options
   */
  bool SetupCommonOptions(bool verbose);

  /**
   * @brief Internal function to extract response information after request
   */
  void ExtractResponseInfo();

  /**
   * @brief Internal function to parse response headers
   */
  void ParseHeaders();

  /**
   * @brief Internal function to reset buffers and state
   */
  void ResetState();

  /**
   * @brief Internal function to perform the actual HTTP request
   */
  bool PerformRequest(bool verbose);
};
}  // namespace plugin_common_curl

#endif  // PLUGINS_COMMON_CURL_CLIENT_CURL_CLIENT_H_