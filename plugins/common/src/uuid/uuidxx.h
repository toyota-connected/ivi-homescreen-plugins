#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace plugin_common::uuidxx {

enum class Variant { Nil, Version1, Version2, Version3, Version4, Version5 };

union uuid {
 private:
  static uuid Generatev4();

 public:
  uint64_t WideIntegers[2]{};
  struct _internalData {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
  } Uuid;
  struct _byteRepresentation {
    uint8_t Data1[4];
    uint8_t Data2[2];
    uint8_t Data3[2];
    uint8_t Data4[8];
  } Bytes;

  bool operator==(const uuid& guid2) const;
  bool operator!=(const uuid& guid2) const;
  bool operator<(const uuid& guid2) const;
  bool operator>(const uuid& guid2) const;

  uuid() = default;

  explicit uuid(const char* uuidString);
  explicit uuid(const std::string& uuidString);
  static uuid FromString(const char* uuidString);
  static uuid FromString(const std::string& uuidString);

  static uuid Generate(const Variant v = Variant::Version4) {
    switch (v) {
      case Variant::Nil:
        return uuid(nullptr);  // special case;
      case Variant::Version1:
      case Variant::Version2:
      case Variant::Version3:
      case Variant::Version5:
        throw std::logic_error("Function not yet implemented");
      case Variant::Version4:
        return Generatev4();
    }
    return uuid(nullptr);
  }

  [[nodiscard]] std::string ToString(bool withBraces = true) const;
};

static_assert(sizeof(uuid) == 2 * sizeof(int64_t),
              "Check uuid type declaration/padding!");
}  // namespace plugin_common::uuidxx
