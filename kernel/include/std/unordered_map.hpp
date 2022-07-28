#pragma once

#include <std/functional.hpp>
#include <std/utility.hpp>
#include <std/vector.hpp>
#include <std/linked_list.hpp>

#include <Luna/misc/log.hpp>

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

        using entry_type = std::pair<Key, T>;


        unordered_map() {
            _map.resize(bucket_size);
        }
        
        T& operator[](const Key& key) {
            auto& list = _map[_hasher(key) % bucket_size];

            for(auto& entry : list)
                if(entry.first == key)
                    return entry.second;

            auto& e = list.push_back({key, {}});
            return e.second;
        }

        bool contains(const Key& key) {
            auto& list = _map[_hasher(key) % bucket_size];

            for(auto& entry : list)
                if(entry.first == key)
                    return true;
            return false;
        }

        struct Iterator {
            Iterator(unordered_map* map, size_t bucket_i, size_t list_i): bucket_i{bucket_i}, list_i{list_i}, map{map} {}
            Iterator(unordered_map* map): bucket_i{0}, list_i{0}, map{map} {
                while(bucket_i < bucket_size && map->_map[bucket_i].size() == 0)
                    bucket_i++;
            }

            entry_type& operator*() { return map->_map[bucket_i][list_i]; }
            entry_type* operator->() { return &map->_map[bucket_i][list_i]; }
            bool operator!=(Iterator it) { return !((bucket_i == it.bucket_i) && (list_i == it.list_i)); }

            void operator++() {
                if(bucket_i == bucket_size)
                    return;
                
                list_i++;
                if(list_i >= map->_map[bucket_i].size()) {
                    bucket_i++;
                    list_i = 0;

                    while(bucket_i < bucket_size && map->_map[bucket_i].size() == 0)
                        bucket_i++;
                }
            }

            private:
            size_t bucket_i, list_i;
            unordered_map* map;
        };

        Iterator begin() { return Iterator{this}; }
        const Iterator begin() const { return Iterator{this}; }

        Iterator end() { return Iterator{this, bucket_size, 0}; }
        const Iterator end() const { return Iterator{this, bucket_size, 0}; }

        Iterator find(const Key& key) {
            size_t bucket_i = _hasher(key) % bucket_size;
            auto& list = _map[bucket_i];

            for(size_t list_i = 0; list_i < list.size(); list_i++)
                if(list[list_i].first == key)
                    return Iterator{this, bucket_i, list_i};

            return end();
        }

        void clear() {
            _map.clear();
            _map.resize(bucket_size);
        }

        private:
        static constexpr size_t bucket_size = 64;
        
        std::vector<std::vector<entry_type>> _map;
        hasher _hasher;
    };
} // namespace std