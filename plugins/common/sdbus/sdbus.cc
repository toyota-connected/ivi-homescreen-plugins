/*
 * Copyright 2020-2025 Toyota Connected North America
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

#include "sdbus.h"

namespace plugin_common_sdbus {

SystemDBus& SystemDBus::Instance() {
  static SystemDBus instance;
  return instance;
}

SystemDBus::SystemDBus() {
  conn_ = sdbus::createSystemBusConnection();
  conn_->enterEventLoopAsync();
}

sdbus::IConnection& SystemDBus::GetConnection() {
  return *conn_;
}

SystemDBus::~SystemDBus() {
  conn_->leaveEventLoop();
}

SessionDBus& SessionDBus::Instance() {
  static SessionDBus instance;
  return instance;
}

SessionDBus::SessionDBus() {
  conn_ = sdbus::createSessionBusConnection();
  conn_->enterEventLoopAsync();
}

sdbus::IConnection& SessionDBus::GetConnection() {
  return *conn_;
}

SessionDBus::~SessionDBus() {
  conn_->leaveEventLoop();
}

}  // namespace plugin_common_sdbus