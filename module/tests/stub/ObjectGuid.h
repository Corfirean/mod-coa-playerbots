// Minimal stand-in for AzerothCore's ObjectGuid so headers that only hold guids as members
// (WorldTask.h) can be unit-tested without a core checkout.
#ifndef COA_PLAYERBOTS_TEST_OBJECT_GUID_STUB_H
#define COA_PLAYERBOTS_TEST_OBJECT_GUID_STUB_H

#include "Define.h"
#include <functional>
#include <string>

class ObjectGuid
{
public:
    ObjectGuid() = default;
    explicit ObjectGuid(uint64 raw) : _raw(raw) { }

    uint64 GetRawValue() const { return _raw; }
    uint64 GetCounter() const { return _raw; }
    bool IsEmpty() const { return _raw == 0; }
    bool IsGameObject() const { return false; }
    std::string ToString() const { return std::to_string(_raw); }

    bool operator==(ObjectGuid const& other) const { return _raw == other._raw; }
    bool operator!=(ObjectGuid const& other) const { return _raw != other._raw; }

    uint64 _raw = 0;

    static ObjectGuid const Empty;
};

// Defined in one translation unit via the test main. We define it here as inline in C++17.
inline ObjectGuid const ObjectGuid::Empty{};

namespace std
{
    template<>
    struct hash<ObjectGuid>
    {
        size_t operator()(ObjectGuid const& guid) const noexcept
        {
            return std::hash<uint64>{}(guid.GetRawValue());
        }
    };
}

#endif
