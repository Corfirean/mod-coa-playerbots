#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include "Define.h"
class DataMap {
public:
    struct Base { virtual ~Base() = default; };
    uint64 revision = 0;
    template<class T> T* Get(std::string const& key) const {
        auto it = values.find(key);
        return it == values.end() ? nullptr : dynamic_cast<T*>(it->second.get());
    }
    template<class T> T* GetDefault(std::string const& key) {
        if (auto* value = Get<T>(key)) return value;
        auto value = std::make_unique<T>(); auto* result = value.get();
        values.emplace(key, std::move(value)); return result;
    }
private:
    std::unordered_map<std::string, std::unique_ptr<Base>> values;
};
