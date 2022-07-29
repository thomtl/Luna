#pragma once

#include <std/utility.hpp>

namespace std {
    template<typename T>
    struct linked_list {
        constexpr linked_list(): head{nullptr}, tail{nullptr}, length{0} {}

        ~linked_list() {
            if(length == 0)
                return;
            
            for(auto* curr = head; curr != nullptr;) {
                auto* ptr = curr;
                curr = curr->next;
                delete ptr;
            }
        }

        linked_list(const linked_list&) = delete;
        linked_list(linked_list&&) = delete;
        linked_list& operator=(const linked_list&) = delete;
        linked_list& operator=(linked_list&&) = delete;

        struct item {
            template<typename... Args>
            item(Args&&... args): entry{T{std::forward<Args>(args)...}}, prev{nullptr}, next{nullptr} {}
            item(const T& entry): entry{entry}, prev{nullptr}, next{nullptr} {}

            T entry;
            item* prev, *next;
        };

        struct iterator {
            iterator(item* entry): entry{entry} {}

            T& operator*() { return entry->entry; }
            T* operator->() const { return &entry->entry; }

            void operator++() { if(entry) entry = entry->next; }
            bool operator==(const iterator& it) { return entry == it.entry; }
            bool operator!=(const iterator& it) { return entry != it.entry; }

            //private:
            item* entry;
        };

        iterator insert(iterator pos, const T& value) {
            if(pos == begin()) {
                push_front(value);
                return iterator{head};
            } else if(pos == end()) {
                push_back(value);
                return iterator{tail};
            }

            auto* new_entry = new item{value};
            DEBUG_ASSERT(pos.entry->prev);// && pos.entry->next);

            auto* previous = pos.entry->prev;

            pos.entry->prev->next = new_entry;                
            pos.entry->prev = new_entry;
            new_entry->prev = previous;
            new_entry->next = pos.entry;
            length++;

            return iterator{new_entry};
        }

        void push_front(const T& value) {
            auto* new_entry = new item{value};

            new_entry->next = head;
            new_entry->prev = nullptr;        

            if(head)
                head->prev = new_entry;

            if(!tail)
                tail = new_entry;

            head = new_entry;

            length++;
        }

        void push_back(const T& value) {
            auto* new_entry = new item{value};

            new_entry->next = nullptr;
            new_entry->prev = tail;

            if(tail)
                tail->next = new_entry;

            if(!head)
                head = new_entry;

            tail = new_entry;
            length++;
        }

        void pop_front() {
            auto* ptr = head;
            if(head->next) {
                head->next->prev = nullptr;
                head = head->next;
            } else {
                head = nullptr;
                tail = nullptr;
            }

            length--;
            delete ptr;
        }

        template<typename... Args>
        T& emplace_back(Args&&... args) {
            auto* new_entry = new item{std::forward<Args>(args)...};

            new_entry->next = nullptr;
            new_entry->prev = tail;

            if(tail)
                tail->next = new_entry;

            if(!head)
                head = new_entry;

            tail = new_entry;
            length++;

            return new_entry->entry; 
        }

        iterator begin() { return iterator{head}; }
        iterator begin() const { return iterator{head}; }

        iterator end() { return iterator{nullptr}; }
        iterator end() const { return iterator{nullptr}; }

        T& front() { DEBUG_ASSERT(head); return head->entry; }
        const T& front() const { DEBUG_ASSERT(head); return head->entry; }

        T& back() { DEBUG_ASSERT(tail); return tail->entry; }
        const T& back() const { DEBUG_ASSERT(tail); return tail->entry; }

        size_t size() const { return length; }

        private:
        item* head, *tail;
        size_t length;
    };
} // namespace std
