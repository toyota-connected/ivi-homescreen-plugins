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

#include "librive_text.h"

#include <iostream>

#include <dlfcn.h>

#include "shared_library.h"

namespace plugin_rive_text {

LibRiveTextExports::LibRiveTextExports(void* lib) {
  if (lib != nullptr) {
    GetFuncAddress(lib, "init", &Initialize);
    GetFuncAddress(lib, "disableFallbackFonts", &DisableFallbackFonts);
    GetFuncAddress(lib, "enableFallbackFonts", &EnableFallbackFonts);
  }
}

LibRiveTextExports* LibRiveText::operator->() const {
  return loadExports(nullptr);
}

LibRiveTextExports* LibRiveText::loadExports(
    const char* library_path = nullptr) {
  static LibRiveTextExports exports = [&] {
    void* lib = dlopen(library_path ? library_path : "librive_text.so",
                       RTLD_NOW | RTLD_LOCAL);

    return LibRiveTextExports(lib);
  }();

  return exports.DisableFallbackFonts ? &exports : nullptr;
}

class LibRiveText LibRiveText;
}  // namespace plugin_rive_text