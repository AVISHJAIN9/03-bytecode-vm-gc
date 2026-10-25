#include "finalizer.hpp"
#include <algorithm>

namespace bvm {

void FinalizerManager::register_finalizer(ObjHeader* obj, FinalizerFn fn) {
    if (!obj || !fn) return;
    obj->set_finalizer_registered(true);
    unfinalized_list_.push_back({obj, std::move(fn)});
}

void FinalizerManager::unregister_finalizer(ObjHeader* obj) {
    if (!obj) return;
    obj->set_finalizer_registered(false);
    for (auto it = unfinalized_list_.begin(); it != unfinalized_list_.end(); ++it) {
        if (it->obj == obj) {
            unfinalized_list_.erase(it);
            break;
        }
    }
}

size_t FinalizerManager::process_unfinalized_objects(GrayWorklist& worklist) {
    size_t moved_to_ready = 0;
    std::vector<FinalizerEntry> still_unfinalized;
    still_unfinalized.reserve(unfinalized_list_.size());

    for (auto& entry : unfinalized_list_) {
        if (!entry.obj) continue;

        if (entry.obj->is_white()) {
            // Object is dead/unreached by mutator roots:
            // Move to ready_queue and resurrect for this cycle so its finalizer can safely run
            entry.obj->mark_gray();
            worklist.push(entry.obj);
            entry.obj->set_finalizer_registered(false);
            entry.obj->set_finalizer_executed(true);

            ready_queue_.push_back(std::move(entry));
            moved_to_ready++;
        } else {
            // Object is still strongly reachable
            still_unfinalized.push_back(std::move(entry));
        }
    }

    unfinalized_list_ = std::move(still_unfinalized);
    return moved_to_ready;
}

size_t FinalizerManager::run_pending_finalizers() {
    size_t count = 0;
    for (auto& entry : ready_queue_) {
        if (entry.obj && entry.fn) {
            entry.fn(entry.obj);
            count++;
        }
    }
    ready_queue_.clear();
    total_finalizers_run_ += count;
    return count;
}

} // namespace bvm
