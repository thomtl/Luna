#include <Luna/misc/debug.hpp>
#include <Luna/misc/log.hpp>

void debug::trace_stack(uintptr_t rbp) {
    struct Frame {
        Frame* rbp;
        uint64_t rip;
    };

    auto* current = (Frame*)rbp;

	print("Stack Trace:\n");
    size_t i = 0;
	while(true) {
		if(!current)
    		break;

        if(current->rip == 0)
            break;
		
        print("  {}: RIP: {:#x}\n", i++, current->rip);		
		
        current = current->rbp;
	}
}

void debug::trace_stack() {
    trace_stack((uintptr_t)__builtin_frame_address(0));
}