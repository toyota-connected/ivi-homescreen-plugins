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

#include "curl_client.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <curl/curl.h>
#include <curl/easy.h>

#include "tools/logging.h"

namespace plugin_common_curl {

CurlClient::CurlClient() : mCode(CURLE_OK) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  mErrorBuffer = std::make_unique<char[]>(CURL_ERROR_SIZE);
  mErrorBuffer[0] = '\0';
}

CurlClient::~CurlClient() {
  if (mConn) {
    curl_easy_cleanup(mConn);
  }

  if (mHeadersList) {
    curl_slist_free_all(mHeadersList);
  }

  curl_global_cleanup();
  mErrorBuffer.reset();
}

int CurlClient::StringWriter(const char* data,
                             const size_t size,
                             const size_t num_mem_block,
                             std::string* writerData) {
  if (data && writerData) {
    writerData->append(data, size * num_mem_block);
  }
  return static_cast<int>(size * num_mem_block);
}

int CurlClient::VectorWriter(char* data,
                             const size_t size,
                             const size_t num_mem_block,
                             std::vector<uint8_t>* writerData) {
  if (data && writerData) {
    auto* u_data = reinterpret_cast<uint8_t*>(data);
    writerData->insert(writerData->end(), u_data,
                       u_data + size * num_mem_block);
  }
  return static_cast<int>(size * num_mem_block);
}

size_t CurlClient::HeaderWriter(char* data,
                                size_t size,
                                size_t num_mem_block,
                                std::string* writerData) {
  if (data && writerData) {
    writerData->append(data, size * num_mem_block);
  }
  return size * num_mem_block;
}

void CurlClient::ResetState() {
  mStringBuffer.clear();
  mVectorBuffer.clear();
  mHeaderBuffer.clear();
  mPostFields.clear();
  mResponseInfo = ResponseInfo{};
  mCode = CURLE_OK;

  if (mConn) {
    curl_easy_cleanup(mConn);
    mConn = nullptr;
  }

  if (mHeadersList) {
    curl_slist_free_all(mHeadersList);
    mHeadersList = nullptr;
  }
}

void CurlClient::SetBearerToken(const std::string& token) {
  if (token.empty()) {
    mAuthHeader.clear();
  } else {
    mAuthHeader = "Authorization: Bearer " + token;
  }
}

void CurlClient::SetTimeout(long timeout_seconds) {
  mTimeout = timeout_seconds;
}

void CurlClient::SetConnectionTimeout(long timeout_seconds) {
  mConnectionTimeout = timeout_seconds;
}

void CurlClient::SetMaxRedirects(long max_redirects) {
  mMaxRedirects = max_redirects;
}

bool CurlClient::SetupCommonOptions(bool verbose) {
  if (!mConn) {
    return false;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_ERRORBUFFER, mErrorBuffer.get());
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set error buffer [{}]",
                  static_cast<int>(mCode));
    return false;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_VERBOSE, verbose ? 1L : 0L);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set verbose mode [{}]",
                  mErrorBuffer.get());
    return false;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_TIMEOUT, mTimeout);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set timeout [{}]",
                  mErrorBuffer.get());
    return false;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_CONNECTTIMEOUT, mConnectionTimeout);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set connection timeout [{}]",
                  mErrorBuffer.get());
    return false;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_FOLLOWLOCATION, 1L);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set follow location [{}]",
                  mErrorBuffer.get());
    return false;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_MAXREDIRS, mMaxRedirects);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set max redirects [{}]",
                  mErrorBuffer.get());
    return false;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_HEADERFUNCTION, HeaderWriter);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set header callback [{}]",
                  mErrorBuffer.get());
    return false;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_HEADERDATA, &mHeaderBuffer);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set header data [{}]",
                  mErrorBuffer.get());
    return false;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_SSL_VERIFYPEER, 1L);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set SSL verify peer [{}]",
                  mErrorBuffer.get());
    return false;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_SSL_VERIFYHOST, 2L);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set SSL verify host [{}]",
                  mErrorBuffer.get());
    return false;
  }

  return true;
}

void CurlClient::ExtractResponseInfo() {
  if (!mConn)
    return;

  curl_easy_getinfo(mConn, CURLINFO_RESPONSE_CODE, &mResponseInfo.http_code);
  curl_easy_getinfo(mConn, CURLINFO_TOTAL_TIME, &mResponseInfo.total_time);

#if LIBCURL_VERSION_NUM >= 0x073700  // 7.55.0
  curl_easy_getinfo(mConn, CURLINFO_SIZE_DOWNLOAD_T,
                    &mResponseInfo.download_size);
  curl_easy_getinfo(mConn, CURLINFO_SIZE_UPLOAD_T, &mResponseInfo.upload_size);
#else
  curl_easy_getinfo(mConn, CURLINFO_SIZE_DOWNLOAD,
                    &mResponseInfo.download_size);
  curl_easy_getinfo(mConn, CURLINFO_SIZE_UPLOAD, &mResponseInfo.upload_size);
#endif

  curl_easy_getinfo(mConn, CURLINFO_REDIRECT_COUNT,
                    &mResponseInfo.redirect_count);

  char* effective_url = nullptr;
  curl_easy_getinfo(mConn, CURLINFO_EFFECTIVE_URL, &effective_url);
  if (effective_url) {
    mResponseInfo.effective_url = effective_url;
  }

  char* content_type = nullptr;
  curl_easy_getinfo(mConn, CURLINFO_CONTENT_TYPE, &content_type);
  if (content_type) {
    mResponseInfo.content_type = content_type;
  }

  ParseHeaders();
}

void CurlClient::ParseHeaders() {
  mResponseInfo.headers.clear();

  if (mHeaderBuffer.empty())
    return;

  std::istringstream stream(mHeaderBuffer);
  std::string line;

  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line.empty() || line.find("HTTP/") == 0) {
      continue;
    }

    size_t colon_pos = line.find(':');
    if (colon_pos != std::string::npos) {
      std::string key = line.substr(0, colon_pos);
      std::string value = line.substr(colon_pos + 1);

      key.erase(0, key.find_first_not_of(" \t"));
      key.erase(key.find_last_not_of(" \t") + 1);
      value.erase(0, value.find_first_not_of(" \t"));
      value.erase(value.find_last_not_of(" \t") + 1);

      if (!key.empty()) {
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        mResponseInfo.headers[key] = value;
      }
    }
  }
}

bool CurlClient::PerformRequest(bool /* verbose */) {
  if (!mConn)
    return false;

  mCode = curl_easy_perform(mConn);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to perform request: {} [{}]",
                  curl_easy_strerror(mCode), mErrorBuffer.get());
    return false;
  }

  // Extract response information
  ExtractResponseInfo();

  spdlog::debug("[CurlClient] Request completed - HTTP {}, {} bytes in {:.2f}s",
                mResponseInfo.http_code, mResponseInfo.download_size,
                mResponseInfo.total_time);

  return true;
}

bool CurlClient::Init(
    const std::string& url,
    const std::vector<std::string>& headers,
    const std::vector<std::pair<std::string, std::string>>& url_form,
    const bool follow_location,
    const bool verbose) {
  // Validate URL
  if (url.empty()) {
    spdlog::error("[CurlClient] URL cannot be empty");
    return false;
  }

  ResetState();

  mConn = curl_easy_init();
  if (mConn == nullptr) {
    spdlog::error("[CurlClient] Failed to create CURL connection");
    return false;
  }

  mUrl = url;
  spdlog::trace("[CurlClient] URL: {}", mUrl);

  mCode = curl_easy_setopt(mConn, CURLOPT_URL, mUrl.c_str());
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set URL [{}]", mErrorBuffer.get());
    return false;
  }

  if (!SetupCommonOptions(false)) {
    return false;
  }

  if (!url_form.empty()) {
    for (const auto& [key, value] : url_form) {
      if (!mPostFields.empty()) {
        mPostFields += "&";
      }
      char* encoded_key =
          curl_easy_escape(mConn, key.c_str(), static_cast<int>(key.length()));
      char* encoded_value = curl_easy_escape(mConn, value.c_str(),
                                             static_cast<int>(value.length()));
      if (encoded_key && encoded_value) {
        mPostFields += encoded_key;
        mPostFields += "=";
        mPostFields += encoded_value;
      }
      curl_free(encoded_key);
      curl_free(encoded_value);
    }
    spdlog::trace("[CurlClient] PostFields: {}", mPostFields);
    curl_easy_setopt(mConn, CURLOPT_POSTFIELDSIZE, mPostFields.length());
    // libcurl does not copy
    curl_easy_setopt(mConn, CURLOPT_POSTFIELDS, mPostFields.c_str());
  }

  if (!mAuthHeader.empty()) {
    mHeadersList = curl_slist_append(mHeadersList, mAuthHeader.c_str());
  }
  for (const auto& header : headers) {
    spdlog::trace("[CurlClient] Header: {}", header);
    mHeadersList = curl_slist_append(mHeadersList, header.c_str());
  }
  if (mHeadersList) {
    mCode = curl_easy_setopt(mConn, CURLOPT_HTTPHEADER, mHeadersList);
    if (mCode != CURLE_OK) {
      spdlog::error("[CurlClient] Failed to set headers option [{}]",
                    mErrorBuffer.get());
      return false;
    }
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_FOLLOWLOCATION,
                           follow_location ? 1L : 0L);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set redirect option [{}]",
                  mErrorBuffer.get());
    return false;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_VERBOSE, verbose ? 1L : 0L);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set verbose", mErrorBuffer.get());
    return false;
  }

  return true;
}

std::string CurlClient::Get(
    const std::string& url,
    const std::vector<std::string>& additional_headers) {
  if (Init(url, additional_headers, {})) {
    return RetrieveContentAsString();
  }
  return "";
}

std::string CurlClient::Post(
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& form_data,
    const std::vector<std::string>& additional_headers) {
  if (Init(url, additional_headers, form_data)) {
    return RetrieveContentAsString();
  }
  return "";
}

std::string CurlClient::Put(
    const std::string& url,
    const std::string& data,
    const std::vector<std::string>& additional_headers) {
  if (url.empty()) {
    spdlog::error("[CurlClient] URL cannot be empty");
    return "";
  }

  ResetState();

  mConn = curl_easy_init();
  if (!mConn) {
    spdlog::error("[CurlClient] Failed to create CURL connection");
    return "";
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_URL, url.c_str());
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set URL [{}]", mErrorBuffer.get());
    return "";
  }

  if (!SetupCommonOptions(false)) {
    return "";
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_CUSTOMREQUEST, "PUT");
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set PUT method [{}]",
                  mErrorBuffer.get());
    return "";
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_POSTFIELDS, data.c_str());
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set PUT data [{}]",
                  mErrorBuffer.get());
    return "";
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_POSTFIELDSIZE, data.length());
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set PUT data size [{}]",
                  mErrorBuffer.get());
    return "";
  }

  if (!mAuthHeader.empty()) {
    mHeadersList = curl_slist_append(mHeadersList, mAuthHeader.c_str());
  }
  for (const auto& header : additional_headers) {
    mHeadersList = curl_slist_append(mHeadersList, header.c_str());
  }
  if (mHeadersList) {
    mCode = curl_easy_setopt(mConn, CURLOPT_HTTPHEADER, mHeadersList);
    if (mCode != CURLE_OK) {
      spdlog::error("[CurlClient] Failed to set headers [{}]",
                    mErrorBuffer.get());
      return "";
    }
  }

  return RetrieveContentAsString();
}

std::string CurlClient::Delete(
    const std::string& url,
    const std::vector<std::string>& additional_headers) {
  if (url.empty()) {
    spdlog::error("[CurlClient] URL cannot be empty");
    return "";
  }

  ResetState();

  mConn = curl_easy_init();
  if (!mConn) {
    spdlog::error("[CurlClient] Failed to create CURL connection");
    return "";
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_URL, url.c_str());
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set URL [{}]", mErrorBuffer.get());
    return "";
  }

  if (!SetupCommonOptions(false)) {
    return "";
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_CUSTOMREQUEST, "DELETE");
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set DELETE method [{}]",
                  mErrorBuffer.get());
    return "";
  }

  if (!mAuthHeader.empty()) {
    mHeadersList = curl_slist_append(mHeadersList, mAuthHeader.c_str());
  }
  for (const auto& header : additional_headers) {
    mHeadersList = curl_slist_append(mHeadersList, header.c_str());
  }
  if (mHeadersList) {
    mCode = curl_easy_setopt(mConn, CURLOPT_HTTPHEADER, mHeadersList);
    if (mCode != CURLE_OK) {
      spdlog::error("[CurlClient] Failed to set headers [{}]",
                    mErrorBuffer.get());
      return "";
    }
  }

  return RetrieveContentAsString();
}

std::string CurlClient::RetrieveContentAsString(const bool verbose) {
  if (mConn == nullptr) {
    spdlog::error("[CurlClient] No connection available");
    return "";
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_VERBOSE, verbose ? 1L : 0L);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set 'CURLOPT_VERBOSE' [{}]",
                  mErrorBuffer.get());
    return "";
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_WRITEFUNCTION, StringWriter);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set writer [{}]", mErrorBuffer.get());
    return "";
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_WRITEDATA, &mStringBuffer);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set write data [{}]",
                  mErrorBuffer.get());
    return "";
  }

  if (!PerformRequest(verbose)) {
    return "";
  }
  return mStringBuffer;
}

const std::vector<uint8_t>& CurlClient::RetrieveContentAsVector(
    const bool verbose) {
  if (mConn == nullptr) {
    spdlog::error("[CurlClient] No connection available");
    mVectorBuffer.clear();
    return mVectorBuffer;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_VERBOSE, verbose ? 1L : 0L);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set 'CURLOPT_VERBOSE' [{}]",
                  mErrorBuffer.get());
    mVectorBuffer.clear();
    return mVectorBuffer;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_WRITEFUNCTION, VectorWriter);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set writer [{}]", mErrorBuffer.get());
    mVectorBuffer.clear();
    return mVectorBuffer;
  }

  mCode = curl_easy_setopt(mConn, CURLOPT_WRITEDATA, &mVectorBuffer);
  if (mCode != CURLE_OK) {
    spdlog::error("[CurlClient] Failed to set write data [{}]",
                  mErrorBuffer.get());
    mVectorBuffer.clear();
    return mVectorBuffer;
  }

  mVectorBuffer.clear();

  if (!PerformRequest(verbose)) {
    mVectorBuffer.clear();
    return mVectorBuffer;
  }

  return mVectorBuffer;
}
}  // namespace plugin_common_curl