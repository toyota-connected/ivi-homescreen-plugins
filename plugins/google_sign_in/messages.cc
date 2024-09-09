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

#undef _HAS_EXCEPTIONS

#include "messages.h"

#include <flutter/basic_message_channel.h>
#include <flutter/binary_messenger.h>
#include <flutter/encodable_value.h>
#include <flutter/method_call.h>
#include <flutter/method_channel.h>

#include <string>

#include "plugins/common/common.h"

namespace google_sign_in_plugin {

using flutter::BasicMessageChannel;
using flutter::CustomEncodableValue;
using flutter::EncodableList;
using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::MethodCall;
using flutter::MethodResult;

// Method Constants
static constexpr auto kMethodInit = "init";
static constexpr auto kMethodSignIn = "signIn";
static constexpr auto kMethodSignInSilently = "signInSilently";
static constexpr auto kMethodGetTokens = "getTokens";
static constexpr auto kMethodSignOut = "signOut";
static constexpr auto kMethodDisconnect = "disconnect";

// Method Argument Constants
static constexpr auto kMethodArgSignInOption = "signInOption";
static constexpr auto kMethodArgScopes = "scopes";
static constexpr auto kMethodArgHostedDomain = "hostedDomain";
static constexpr auto kMethodArgClientId = "clientId";
static constexpr auto kMethodArgServerClientId = "serverClientId";
static constexpr auto kMethodArgForceCodeForRefreshToken =
    "forceCodeForRefreshToken";
static constexpr auto kMethodArgShouldRecoverAuth = "shouldRecoverAuth";

static constexpr auto kMethodResponseKeyEmail = "email";

/// The codec used by GoogleSignInApi.
const flutter::StandardMethodCodec& GoogleSignInApi::GetCodec() {
  return flutter::StandardMethodCodec::GetInstance();
}

// Sets up an instance of `GoogleSignInApi` to handle messages through the
// `binary_messenger`.
void GoogleSignInApi::SetUp(flutter::BinaryMessenger* binary_messenger,
                            GoogleSignInApi* api) {
  {
    const auto channel = std::make_unique<flutter::MethodChannel<>>(
        binary_messenger, "plugins.flutter.io/google_sign_in", &GetCodec());
    if (api != nullptr) {
      channel->SetMethodCallHandler(
          [api](const MethodCall<>& call,
                std::unique_ptr<MethodResult<>> result) {
            if (const auto& method = call.method_name();
                method == kMethodInit) {
              SPDLOG_DEBUG("[google_sign_in] <init>");

              const auto args = std::get<EncodableMap>(*call.arguments());
              if (args.empty()) {
                result->Error("invalid_arguments", "");
                return;
              }

              std::string signInOption;
              std::vector<std::string> requestedScopes;
              std::string hostedDomain;
              std::string clientId;
              std::string serverClientId;
              bool forceCodeForRefreshToken{};

              for (const auto& [fst, snd] : args) {
                if (const auto key = std::get<std::string>(fst);
                    key == kMethodArgSignInOption && !snd.IsNull()) {
                  signInOption.assign(std::get<std::string>(snd));
                } else if (key == kMethodArgScopes && !snd.IsNull()) {
                  auto requestedScopes_ =
                      std::get<std::vector<EncodableValue>>(snd);
                  for (auto& scope : requestedScopes_) {
                    if (!scope.IsNull()) {
                      auto val = std::get<std::string>(scope);
                      requestedScopes.push_back(std::move(val));
                    }
                  }
                } else if (key == kMethodArgHostedDomain && !snd.IsNull()) {
                  hostedDomain.assign(std::get<std::string>(snd));
                } else if (key == kMethodArgClientId && !snd.IsNull()) {
                  clientId.assign(std::get<std::string>(snd));
                } else if (key == kMethodArgServerClientId && !snd.IsNull()) {
                  serverClientId.assign(std::get<std::string>(snd));
                } else if (key == kMethodArgForceCodeForRefreshToken &&
                           !snd.IsNull()) {
                  forceCodeForRefreshToken = std::get<bool>(snd);
                }
              }

              api->Init(requestedScopes, std::move(hostedDomain),
                        std::move(signInOption), std::move(clientId),
                        std::move(serverClientId), forceCodeForRefreshToken);
              result->Success();
            } else if (method == kMethodSignIn) {
              SPDLOG_DEBUG("[google_sign_in] <signIn>");
              result->Success(api->GetUserData());
            } else if (method == kMethodSignInSilently) {
              SPDLOG_DEBUG("[google_sign_in] <signInSilently>");
              result->Success(api->GetUserData());
            } else if (method == kMethodGetTokens) {
              SPDLOG_DEBUG("[google_sign_in] <getTokens>");
              const auto args = std::get<EncodableMap>(*call.arguments());
              if (args.empty()) {
                result->Error("invalid_arguments", "");
                return;
              }
              std::string email;
              bool shouldRecoverAuth{};
              for (const auto& [fst, snd] : args) {
                if (const auto key = std::get<std::string>(fst);
                    key == kMethodResponseKeyEmail && !snd.IsNull()) {
                  email.assign(std::get<std::string>(snd));
                } else if (key == kMethodArgShouldRecoverAuth &&
                           !snd.IsNull()) {
                  shouldRecoverAuth = std::get<bool>(snd);
                }
              }
              SPDLOG_DEBUG("\temail: [{}]", email);
              SPDLOG_DEBUG("\tshouldRecoverAuth: {}", shouldRecoverAuth);
              result->Success(api->GetTokens(email, shouldRecoverAuth));
            } else if (method == kMethodSignOut) {
              SPDLOG_DEBUG("[google_sign_in] <signOut>");
              result->Success(api->GetUserData());
            } else if (method == kMethodDisconnect) {
              SPDLOG_DEBUG("[google_sign_in] <disconnect>");
              result->Success(api->GetUserData());
            }

            return result->Success();
          });
    } else {
      channel->SetMethodCallHandler(nullptr);
    }
  }
}

}  // namespace google_sign_in_plugin