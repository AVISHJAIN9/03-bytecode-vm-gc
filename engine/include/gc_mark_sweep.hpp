#ifndef BYTECODE_VM_GC_MARK_SWEEP_HPP
#define BYTECODE_VM_GC_MARK_SWEEP_HPP

#include "gc_types.hpp"
#include "root_set.hpp"
#include "allocator.hpp"
#include <cstddef>
#include <cstdint>

namespace bvm {

class VirtualMachine;

/// Exact Tracing Mark-and-Sweep Garbage Collector (Features 19 & 20)
class MarkSweepCollector {
private:
    GrayWorklist worklist_;
    GCStats stats_;

public:
    MarkSweepCollector() = default;
    ~MarkSweepCollector() = default;

    /// Marks a single heap object gray and pushes it to the worklist if white
    void mark_object(ObjHeader* obj);

    /// Non-recursively drains the gray worklist until empty, promoting visited objects to black
    void drain_gray_worklist();

    /// Reclaims all unvisited (white) objects and resets surviving black objects to white
    size_t sweep_phase(MemoryAllocator& allocator);

    /// Runs a full Stop-The-World (STW) mark-and-sweep garbage collection cycle
    size_t collect_garbage(VirtualMachine& vm);

    [[nodiscard]] const GCStats& stats() const noexcept { return stats_; }
    [[nodiscard]] GCStats& stats() noexcept { return stats_; }
    [[nodiscard]] GrayWorklist& worklist() noexcept { return worklist_; }
};

} // namespace bvm

#endif // BYTECODE_VM_GC_MARK_SWEEP_HPP
