#ifndef BYTECODE_VM_GC_HOOKS_HPP
#define BYTECODE_VM_GC_HOOKS_HPP

#include "safepoint.hpp"
#include "write_barrier.hpp"
#include "incremental_gc.hpp"
#include "gc_mark_sweep.hpp"

namespace bvm {

class VirtualMachine;

/// VM Garbage Collection & Mutator Hook Integration (Features 22 - 24)
class VMGCHooks {
public:
    /// Inlined safepoint poll executed at loop back-edges and call/return boundaries
    static inline void poll_safepoint(VirtualMachine& vm, SafepointMechanism& safepoint) {
        if (__builtin_expect(safepoint.is_safepoint_requested(), 0)) {
            safepoint.enter_safepoint(vm);
        }
    }

    /// Mutator write barrier invoked on container mutations (instances, arrays, upvalues)
    static inline void notify_write_barrier(
        IncrementalGC& inc_gc,
        ObjHeader* container,
        const Value& new_val
    ) {
        inc_gc.notify_field_write(container, new_val);
    }

    /// Dynamic allocation watermark check
    static inline void check_allocation_watermark(
        VirtualMachine& vm,
        IncrementalGC& inc_gc,
        size_t current_heap_bytes
    ) {
        if (__builtin_expect(inc_gc.should_trigger(current_heap_bytes), 0)) {
            inc_gc.step(vm, 64);
        }
    }
};

} // namespace bvm

#endif // BYTECODE_VM_GC_HOOKS_HPP
