#pragma once

#include <Luna/common.hpp>
#include <std/bitmap.hpp>

namespace svm {
    class AsidManager {
        public:
        AsidManager(): _asids{} {}
        AsidManager(uint32_t n_asids);
        uint32_t alloc();
        void free(uint32_t asid);

        private:
        std::bitmap _asids;
    };

    void invlpga(uint32_t asid, uintptr_t va);
} // namespace svm::asid