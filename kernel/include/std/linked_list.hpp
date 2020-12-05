#pragma once

#include <std/utility.hpp>

namespace std {
    template<typename T>
    struct linked_list {
        linked_list(): head{nullptr}, tail{nullptr}, length{0} {}

        template<typename... Args>
        T& emplace_back(Args&&... args) {
            auto* new_entry = new item{std::forward<Args>(args)...};

            new_entry->next = nullptr;
            
            if(length == 0) {
                head = new_entry;
                tail = new_entry;

                new_entry->prev = nullptr;
            } else {
                tail->next = new_entry;
                new_entry->prev = tail;
                tail = new_entry;
                length++;
            }

            length++;

            return new_entry->entry; 
        }

        struct item {
            template<typename... Args>
            item(Args&&... args): entry{T{std::forward<Args>(args)...}} {}
            T entry;
            item* prev, *next;
        };

        struct iterator {
            iterator(item* entry): entry{entry} {}
            T& operator*() { return entry->entry; }
            void operator++() { if(entry) entry = entry->next; }
            bool operator!=(iterator it) { return entry != it.entry; }

            private:
            item* entry;
        };

        struct const_iterator {
            const_iterator(const item* entry): entry{entry} {}
            const T& operator*() { return entry->entry; }
            void operator++() { if(entry) entry = entry->next; }
            bool operator!=(const_iterator it) { return entry != it.entry; }

            private:
            const item* entry;
        };

        iterator begin() { return iterator{head}; }
        iterator end() { return iterator{nullptr}; }

        const_iterator begin() const { return const_iterator{head}; }
        const_iterator end() const { return const_iterator{nullptr}; }

        private:
        item* head, *tail;
        size_t length;
    };
} // namespace std
