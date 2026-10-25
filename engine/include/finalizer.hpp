#ifndef BYTECODE_VM_FINALIZER_HPP
#define BYTECODE_VM_FINALIZER_HPP

#include "object.hpp"
#include "gc_types.hpp"
#include <vector>
#include <functional>
#include <cstddef>

namespace bvm {

using FinalizerFn = std::function<void(ObjHeader*)>;

struct FinalizerEntry {
    ObjHeader* obj{nullptr};
    FinalizerFn fn;
};

/// Safe Lifecycle Finalizer Subsystem with Resurrection Protection (Feature 29)
class FinalizerManager {
private:
    std::vector<FinalizerEntry> unfinalized_list_;
    std::vector<FinalizerEntry> ready_queue_;
    size_t total_finalizers_run_{0};

public:
    FinalizerManager() = default;
    ~FinalizerManager() = default;

    FinalizerManager(const FinalizerManager&) = delete;
    FinalizerManager& operator=(const FinalizerManager&) = delete;

    /// Registers a cleanup finalizer for an object
    void register_finalizer(ObjHeader* obj, FinalizerFn fn);

    /// Unregisters an object from finalization
    void unregister_finalizer(ObjHeader* obj);

    /// Discovers newly dead objects with pending finalizers, moves them to the ready queue,
    /// and shades them to Gray so they survive the current GC cycle to execute cleanly
    size_t process_unfinalized_objects(GrayWorklist& worklist);

    /// Runs all pending finalizers in the ready queue and clears the queue
    size_t run_pending_finalizers();

    [[nodiscard]] size_t unfinalized_count() const noexcept { return unfinalized_list_.size(); }
    [[nodiscard]] size_t ready_count() const noexcept { return ready_queue_.size(); }
    [[nodiscard]] size_t total_run_count() const noexcept { return total_finalizers_run_; }

    void clear() noexcept {
        unfinalized_list_.clear();
        ready_queue_.clear();
        total_finalizers_run_ = 0;
    }
};

} // namespace bvm

#endif // BYTECODE_VM_FINALIZER_HPP
