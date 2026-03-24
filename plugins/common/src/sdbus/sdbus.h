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

#ifndef PLUGINS_COMMON_SDBUS_H_
#define PLUGINS_COMMON_SDBUS_H_

#include <sdbus-c++/sdbus-c++.h>
#include <memory>

namespace plugin_common_sdbus {

class SystemDBus final {
 public:
  static SystemDBus& Instance();
  sdbus::IConnection& GetConnection();

  SystemDBus(const SystemDBus&) = delete;
  SystemDBus& operator=(const SystemDBus&) = delete;
  SystemDBus(SystemDBus&&) = delete;
  SystemDBus& operator=(SystemDBus&&) = delete;

 private:
  SystemDBus();
  ~SystemDBus();

  std::unique_ptr<sdbus::IConnection> conn_;
};

/***
 * making this a singleton for now.
 * TODO: track session state using login1 via system connection
 ***/
class SessionDBus final {
 public:
  static SessionDBus& Instance();
  sdbus::IConnection& GetConnection();

  SessionDBus(const SessionDBus&) = delete;
  SessionDBus& operator=(const SessionDBus&) = delete;
  SessionDBus(SessionDBus&&) = delete;
  SessionDBus& operator=(SessionDBus&&) = delete;

 private:
  SessionDBus();
  ~SessionDBus();

  std::unique_ptr<sdbus::IConnection> conn_;
};
}  // namespace plugin_common_sdbus

#endif  // PLUGINS_COMMON_SDBUS_H_