#include "incremental_gc.hpp"
#include "vm.hpp"
#include <algorithm>
#include <chrono>

namespace bvm {

bool IncrementalGC::step(VirtualMachine& vm, size_t work_budget) {
    auto step_start = std::chrono::steady_clock::now();
    stats_.incremental_steps++;

    if (phase_ == GCPhase::IDLE) {
        phase_ = GCPhase::ROOT_SCAN;
        auto step_end = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(step_end - step_start).count();
        stats_.record_pause(static_cast<uint64_t>(elapsed_us));
        return false;
    }

    if (phase_ == GCPhase::ROOT_SCAN) {
        stats_.bytes_allocated_before = vm.allocator().total_allocated_bytes();

        std::vector<ObjHeader*> roots;
        RootSetEnumerator::enumerate(vm, vm.handle_manager(), roots);

        for (ObjHeader* root : roots) {
            if (root && root->is_white()) {
                root->mark_gray();
                worklist_.push(root);
            }
        }

        phase_ = GCPhase::MARKING;
        auto step_end = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(step_end - step_start).count();
        stats_.record_pause(static_cast<uint64_t>(elapsed_us));
        return false;
    }

    if (phase_ == GCPhase::MARKING) {
        while (work_budget > 0 && !worklist_.empty()) {
            ObjHeader* obj = worklist_.pop();
            if (obj) {
                obj->mark_black();
                visit_children(obj, [this](ObjHeader* child) {
                    if (child && child->is_white()) {
                        child->mark_gray();
                        worklist_.push(child);
                    }
                });
            }
            work_budget--;
        }

        if (worklist_.empty()) {
            phase_ = GCPhase::SWEEPING;
            sweep_cursor_ = vm.allocator().all_objects_ptr();
        }
        auto step_end = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(step_end - step_start).count();
        stats_.record_pause(static_cast<uint64_t>(elapsed_us));
        return false;
    }

    if (phase_ == GCPhase::SWEEPING) {
        while (work_budget > 0 && sweep_cursor_ && *sweep_cursor_ != nullptr) {
            ObjHeader* obj = *sweep_cursor_;
            if (obj->is_white() && !obj->is_pinned()) {
                *sweep_cursor_ = obj->next;
                stats_.total_bytes_reclaimed += obj->size_bytes;
                stats_.objects_freed_total++;
                vm.allocator().free_object(obj);
            } else {
                obj->unmark_white();
                stats_.objects_survived_last_cycle++;
                sweep_cursor_ = &obj->next;
            }
            work_budget--;
        }

        if (!sweep_cursor_ || *sweep_cursor_ == nullptr) {
            phase_ = GCPhase::IDLE;
            stats_.total_collections++;
            stats_.bytes_allocated_after = vm.allocator().total_allocated_bytes();
            next_gc_threshold_ = std::max(
                static_cast<size_t>(stats_.bytes_allocated_after * growth_factor_),
                static_cast<size_t>(65536)
            );

            auto step_end = std::chrono::steady_clock::now();
            auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(step_end - step_start).count();
            stats_.record_pause(static_cast<uint64_t>(elapsed_us));
            return true; // Cycle completed
        }
    }

    auto step_end = std::chrono::steady_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(step_end - step_start).count();
    stats_.record_pause(static_cast<uint64_t>(elapsed_us));
    return false; // Cycle still in progress
}

size_t IncrementalGC::collect_all(VirtualMachine& vm) {
    size_t before = stats_.total_bytes_reclaimed;
    // Step repeatedly with unlimited budget until returning to IDLE
    while (!step(vm, 1000000)) {}
    return stats_.total_bytes_reclaimed - before;
}

} // namespace bvm
