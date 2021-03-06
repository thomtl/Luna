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
            using entry_type = std::pair<Key, T>;

            Iterator(unordered_map* map, entry_type* entry): map{map}, entry{entry} {}
            Iterator(unordered_map* map): map{map} {
                list = &map->_map[bucket_i];

                while(list->size() == 0 && bucket_i < bucket_size)
                    list = &map->_map[++bucket_i];

                entry = &((*list)[list_i]);
            }

            entry_type& operator*() { return *entry; }
            bool operator!=(Iterator it) { return entry != it.entry; }

            void operator++() {
                list_i++;
                if(list_i >= list->size()) {
                    bucket_i++;
                    if(bucket_i >= bucket_size) {
                        entry = nullptr;
                        return;
                    }
                    
                    list_i = 0;
                }

                entry = &((*list)[list_i]);
            }

            private:
            size_t bucket_i = 0, list_i = 0;
            unordered_map* map;
            std::vector<entry_type>* list;
            entry_type* entry;
        };

        Iterator begin() { return Iterator{this}; }
        const Iterator begin() const { return Iterator{this}; }
        Iterator end() { return Iterator{this, nullptr}; }
        const Iterator end() const { return Iterator{this, nullptr}; }

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
