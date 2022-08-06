#pragma once

#include <std/utility.hpp>
#include <std/concepts.hpp>

namespace std {
    namespace intrusive {
        template<typename T> // CRTP
        struct list_node {
            list_node* next, *prev;

            T& item() { return *static_cast<T*>(this); }
            const T& item() const { return *static_cast<const T*>(this); }
        };

        template<typename T> requires std::derived_from<T, list_node<T>>
        struct linked_list {
            using node_type = list_node<T>;

            constexpr linked_list(): head{nullptr}, tail{nullptr}, length{0} { }


            linked_list(const linked_list&) = delete;
            linked_list(linked_list&&) = delete;
            linked_list& operator=(const linked_list&) = delete;
            linked_list& operator=(linked_list&&) = delete;

            struct iterator {
                iterator(node_type* entry): entry{entry} {}

                T& operator*() { return entry->item(); }
                T* operator->() const { return &entry->item(); }

                void operator++() { if(entry) entry = entry->next; }
                bool operator==(const iterator& it) const { return entry == it.entry; }
                bool operator!=(const iterator& it) const { return entry != it.entry; }

                //private:
                node_type* entry;
            };

            iterator insert(iterator pos, node_type* node) {
                if(pos == begin()) {
                    push_front(node);
                    return iterator{head};
                } else if(pos == end()) {
                    push_back(node);
                    return iterator{tail};
                }

                DEBUG_ASSERT(pos.entry->prev);

                auto* previous = pos.entry->prev;

                pos.entry->prev->next = node;                
                pos.entry->prev = node;
                node->prev = previous;
                node->next = pos.entry;
                length++;

                return iterator{node};
            }

            void push_front(node_type* node) {
                node->next = head;
                node->prev = nullptr;        

                if(head)
                    head->prev = node;

                if(!tail)
                    tail = node;

                head = node;

                length++;
            }

            void push_back(node_type* node) {
                node->next = nullptr;
                node->prev = tail;

                if(tail)
                    tail->next = node;

                if(!head)
                    head = node;

                tail = node;
                length++;
            }

            void pop_front() {
                if(head->next) {
                    head->next->prev = nullptr;
                    head = head->next;
                } else {
                    head = nullptr;
                    tail = nullptr;
                }

                length--;
            }

            void pop_back() {
                if(tail->prev) {
                    tail->prev->next = nullptr;
                    tail = tail->prev;
                } else {
                    head = nullptr;
                    tail = nullptr;
                }

                length--;
            }

            void erase(node_type* node) {
                if(node == head) {
                    pop_front();
                    return;
                } else if(node == tail) {
                    pop_back();
                    return;
                }

                DEBUG_ASSERT(node->prev && node->next);

                node->prev->next = node->next;
                node->next->prev = node->prev;

                node->prev = nullptr;
                node->next = nullptr;

                length--;
            }

            iterator begin() { return iterator{head}; }
            iterator begin() const { return iterator{head}; }

            iterator end() { return iterator{nullptr}; }
            iterator end() const { return iterator{nullptr}; }

            T& front() { DEBUG_ASSERT(head); return head->item(); }
            const T& front() const { DEBUG_ASSERT(head); return head->item(); }

            T& back() { DEBUG_ASSERT(tail); return tail->item(); }
            const T& back() const { DEBUG_ASSERT(tail); return tail->item(); }

            size_t size() const { return length; }

            private:
            node_type* head, *tail;
            size_t length;
        };
    } // namespace intrusive
    

    template<typename T>
    struct linked_list {
        constexpr linked_list(): _list{} {}

        ~linked_list() {
            if(_list.size() == 0)
                return;
            
            for(auto it = _list.begin(); it != _list.end();) {
                auto* ptr = &it.entry->item();
                ++it;;
                delete ptr;
            }
        }

        linked_list(const linked_list&) = delete;
        linked_list(linked_list&&) = delete;
        linked_list& operator=(const linked_list&) = delete;
        linked_list& operator=(linked_list&&) = delete;

        struct item : public intrusive::list_node<item> {
            template<typename... Args>
            item(Args&&... args): entry{std::forward<Args>(args)...} {}
            item(const T& entry): entry{entry} {}

            T entry;
        };
        
        struct iterator {
            iterator(intrusive::linked_list<item>::iterator it): it{it} {}
            T& operator*() { return it.entry->item().entry; }
            T* operator->() const { return &it.entry->item().entry; }

            void operator++() { ++it; }
            bool operator==(const iterator& other) const { return it == other.it; }
            bool operator!=(const iterator& other) const { return it != other.it; }

            //private:
            intrusive::linked_list<item>::iterator it;
        };


        iterator insert(iterator pos, const T& value) {
            auto* new_entry = new item{value};
            return _list.insert(pos.it, new_entry);
        }

        void push_front(const T& value) {
            auto* new_entry = new item{value};

            _list.push_front(new_entry);
        }

        void push_back(const T& value) {
            auto* new_entry = new item{value};
            _list.push_back(new_entry);
        }

        void pop_front() {
            auto* ptr = &_list.front();
            _list.pop_front();
            delete ptr;
        }

        void pop_back() {
            auto* ptr = &_list.back();
            _list.pop_back();
            delete ptr;
        }

        template<typename... Args>
        T& emplace_back(Args&&... args) {
            auto* new_entry = new item{std::forward<Args>(args)...};
            _list.push_back(new_entry);
            return new_entry->entry;
        }

        iterator begin() { return iterator{_list.begin()}; }
        iterator begin() const { return iterator{_list.begin()}; }

        iterator end() { return iterator{_list.end()}; }
        iterator end() const { return iterator{_list.end()}; }

        T& front() { return _list.front().entry; }
        const T& front() const { return _list.front().entry; }

        T& back() { return _list.back().entry; }
        const T& back() const { return _list.back().entry; }

        size_t size() const { return _list.size(); }

        private:
        intrusive::linked_list<item> _list;
    };
} // namespace std
