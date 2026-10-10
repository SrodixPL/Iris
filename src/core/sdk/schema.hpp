#pragma once
#include "core/sdk/types.hpp"
#include <cstddef>
namespace iris::sdk {
struct SchemaField {
    Address name;
    Address type;
    int offset;
    std::byte metadata[0x0c];
};
struct SchemaClassInfo {
    std::byte header[0x20];
    int size;
    std::uint16_t field_count;
    std::byte metadata[0x0a];
    Address fields;
};
static_assert(sizeof(SchemaField) == 0x20);
static_assert(offsetof(SchemaField, offset) == 0x10);
static_assert(offsetof(SchemaClassInfo, size) == 0x20);
static_assert(offsetof(SchemaClassInfo, field_count) == 0x24);
static_assert(offsetof(SchemaClassInfo, fields) == 0x30);
}
