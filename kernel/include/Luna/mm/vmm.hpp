#pragma once

#include <Luna/common.hpp>
#include <Luna/cpu/paging.hpp>

namespace vmm
{
    void init_bsp();
    bool is_canonical(uintptr_t addr);
    paging::Context create_context();

    class KernelVmm {
        public:
        static paging::Context& get_instance(){
            return _instance();
        }

        KernelVmm(const KernelVmm&) = delete;
        void operator=(const KernelVmm&) = delete;
        private:
        KernelVmm() = delete;
        static paging::Context& _instance();
    };
} // namespace vmm
