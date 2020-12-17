#pragma once

#include <std/functional.hpp>
#include <std/utility.hpp>
#include <std/vector.hpp>

namespace std
{
    template<typename Key, typename T, typename Hash = std::hash<Key>>
    class unordered_map {
        public:
        using key_type = Key;
        using mapped_type = T;
        using value_type = std::pair<const Key, T>;
        using hasher = Hash;

        using reference = value_type&;
        using const_reference = const value_type&;

        // TODO: Normal hashmap
        T& operator[](const Key& key) {
            auto correct_hash = _hasher(key);

            for(auto& e : _map)
                if(e.first == correct_hash)
                    return e.second;

            auto& e = _map.push_back(entry_type{correct_hash, {}});
            return e.second;
        }

        bool contains(const Key& key) {
            auto hash = _hasher(key);

            for(auto& e : _map)
                if(e.first == hash)
                    return true;

            return false;
        }

        auto begin() { return _map.begin(); }
        const auto begin() const { return _map.begin(); }
        auto end() { return _map.end(); }
        const auto end() const { return _map.end(); }

        private:
        using entry_type = std::pair<size_t, T>;
        std::vector<entry_type> _map;
        hasher _hasher;
    };
} // namespace std
