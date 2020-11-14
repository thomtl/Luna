#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/paging.hpp>

namespace vmm
{
    void init_bsp();
    paging::context create_context();

    class kernel_vmm {
        public:
        static paging::context& get_instance(){
            return _instance();
        }

        kernel_vmm(const kernel_vmm&) = delete;
        void operator=(const kernel_vmm&) = delete;
        private:
        kernel_vmm() = delete;
        static paging::context& _instance();
    };
} // namespace vmm
