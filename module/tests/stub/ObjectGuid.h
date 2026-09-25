// Minimal stand-in for AzerothCore's ObjectGuid so headers that only hold guids as members
// (WorldTask.h) can be unit-tested without a core checkout.
#ifndef COA_PLAYERBOTS_TEST_OBJECT_GUID_STUB_H
#define COA_PLAYERBOTS_TEST_OBJECT_GUID_STUB_H

#include "Define.h"

class ObjectGuid
{
public:
    ObjectGuid() = default;
    explicit ObjectGuid(uint64 raw) : _raw(raw) { }

    uint64 GetRawValue() const { return _raw; }
    bool IsEmpty() const { return _raw == 0; }
    bool operator==(ObjectGuid const& other) const { return _raw == other._raw; }

private:
    uint64 _raw = 0;
};

#endif
