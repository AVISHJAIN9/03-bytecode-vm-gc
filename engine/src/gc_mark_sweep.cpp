#include "gc_mark_sweep.hpp"
#include "vm.hpp"
#include <chrono>

namespace bvm {

void MarkSweepCollector::mark_object(ObjHeader* obj) {
    if (!obj) return;
    if (obj->is_white()) {
        obj->mark_gray();
        worklist_.push(obj);
    }
}

void MarkSweepCollector::drain_gray_worklist() {
    while (!worklist_.empty()) {
        ObjHeader* curr = worklist_.pop();
        if (!curr) continue;

        // Promote popped object to Black (visited all references)
        curr->mark_black();

        // Traverse child pointers and shade white children to gray
        visit_children(curr, [this](ObjHeader* child) {
            if (child && child->is_white()) {
                child->mark_gray();
                worklist_.push(child);
            }
        });
    }
}

size_t MarkSweepCollector::sweep_phase(MemoryAllocator& allocator) {
    size_t bytes_reclaimed = 0;
    size_t objects_freed = 0;
    size_t objects_survived = 0;

    ObjHeader** curr_ptr = allocator.all_objects_ptr();
    while (*curr_ptr != nullptr) {
        ObjHeader* obj = *curr_ptr;
        if (obj->is_white() && !obj->is_pinned()) {
            // Unlink unreached object from global allocation list
            *curr_ptr = obj->next;
            bytes_reclaimed += obj->size_bytes;
            objects_freed++;
            allocator.free_object(obj);
        } else {
            // Live survivor: unmark and reset color to white for next cycle
            obj->unmark_white();
            objects_survived++;
            curr_ptr = &obj->next;
        }
    }

    stats_.total_bytes_reclaimed += bytes_reclaimed;
    stats_.objects_freed_total += objects_freed;
    stats_.objects_survived_last_cycle = objects_survived;

    return bytes_reclaimed;
}

size_t MarkSweepCollector::collect_garbage(VirtualMachine& vm) {
    auto start_time = std::chrono::steady_clock::now();
    stats_.total_collections++;
    stats_.stw_collections++;
    stats_.bytes_allocated_before = vm.allocator().total_allocated_bytes();

    // 1. Enumerate all live roots
    std::vector<ObjHeader*> roots;
    RootSetEnumerator::enumerate(vm, vm.handle_manager(), roots);

    // 2. Mark roots Gray and seed the worklist
    for (ObjHeader* root : roots) {
        mark_object(root);
    }

    // 3. Drain Gray worklist to completion (Feature 19)
    drain_gray_worklist();

    // 3a. Process unfinalized objects: discover newly dead objects with finalizers,
    //     move to ready queue, and resurrect for this cycle (Feature 29)
    size_t unfinalized_resurrected = vm.finalizer_manager().process_unfinalized_objects(worklist_);
    if (unfinalized_resurrected > 0) {
        drain_gray_worklist();
    }

    // 3b. Clear dead weak references (Feature 28)
    size_t weak_cleared = vm.weak_ref_manager().clear_dead_references();
    vm.gc_telemetry().record_weak_cleared(weak_cleared);

    // 3c. Clear dead weak handles (Feature 27)
    vm.handle_table().clear_dead_weak_handles();

    // 4. Sweep unreached White objects (Feature 20)
    size_t bytes_reclaimed = sweep_phase(vm.allocator());
    stats_.bytes_allocated_after = vm.allocator().total_allocated_bytes();

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    auto elapsed_us = elapsed_ns / 1000;
    stats_.record_pause(static_cast<uint64_t>(elapsed_us));

    // Telemetry tracking (Feature 30)
    vm.gc_telemetry().record_major_gc(static_cast<uint64_t>(elapsed_ns), bytes_reclaimed);

    return bytes_reclaimed;
}

} // namespace bvm
