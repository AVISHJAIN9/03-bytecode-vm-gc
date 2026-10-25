#ifndef BYTECODE_VM_INCREMENTAL_GC_HPP
#define BYTECODE_VM_INCREMENTAL_GC_HPP

#include "gc_types.hpp"
#include "write_barrier.hpp"
#include "root_set.hpp"
#include "allocator.hpp"
#include <cstddef>
#include <cstdint>
#include <chrono>

namespace bvm {

class VirtualMachine;

/// Incremental Garbage Collector with Bounded-Slice Interleaving (Feature 24)
class IncrementalGC {
private:
    GCPhase phase_{GCPhase::IDLE};
    GrayWorklist worklist_;
    GCStats stats_;

    ObjHeader** sweep_cursor_{nullptr};
    size_t next_gc_threshold_{65536}; // Initial watermark: 64 KB
    double growth_factor_{1.5};

public:
    IncrementalGC() = default;
    ~IncrementalGC() = default;

    /// Advances the incremental GC state machine by up to `work_budget` units of work
    /// Returns true if a full GC cycle was completed during this step
    bool step(VirtualMachine& vm, size_t work_budget = 64);

    /// Full STW collection shortcut (runs incremental steps until IDLE)
    size_t collect_all(VirtualMachine& vm);

    /// Checks if mutator allocation volume crossed next_gc_threshold_
    [[nodiscard]] bool should_trigger(size_t current_heap_bytes) const noexcept {
        return current_heap_bytes >= next_gc_threshold_;
    }

    /// Mutator write barrier hook
    inline void notify_field_write(ObjHeader* container, const Value& new_val) {
        WriteBarrier::on_field_write(phase_, worklist_, container, new_val, &stats_.write_barriers_triggered);
    }

    [[nodiscard]] GCPhase phase() const noexcept { return phase_; }
    [[nodiscard]] const GCStats& stats() const noexcept { return stats_; }
    [[nodiscard]] GCStats& stats() noexcept { return stats_; }
    [[nodiscard]] GrayWorklist& worklist() noexcept { return worklist_; }
    [[nodiscard]] size_t next_threshold() const noexcept { return next_gc_threshold_; }
    void set_next_threshold(size_t threshold) noexcept { next_gc_threshold_ = threshold; }
    void set_growth_factor(double factor) noexcept { growth_factor_ = factor; }
};

} // namespace bvm

#endif // BYTECODE_VM_INCREMENTAL_GC_HPP
