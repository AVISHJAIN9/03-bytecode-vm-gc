#include "weak_ref.hpp"
#include <algorithm>

namespace bvm {

void WeakReferenceManager::register_weak_ref(ObjWeakRef* ref) {
    if (ref) {
        active_refs_.push_back(ref);
    }
}

void WeakReferenceManager::unregister_weak_ref(ObjWeakRef* ref) {
    if (!ref) return;
    for (auto it = active_refs_.begin(); it != active_refs_.end(); ++it) {
        if (*it == ref) {
            active_refs_.erase(it);
            break;
        }
    }
}

size_t WeakReferenceManager::clear_dead_references() {
    size_t cleared_this_cycle = 0;
    for (ObjWeakRef* ref : active_refs_) {
        if (!ref) continue;
        if (ref->referent != nullptr) {
            if (ref->referent->is_white()) {
                ref->referent = nullptr;
                cleared_this_cycle++;
                total_cleared_++;
            }
        }
    }

    // Prune any dead weak reference wrapper objects whose own header is white
    auto it = std::remove_if(active_refs_.begin(), active_refs_.end(), [](ObjWeakRef* ref) {
        return ref == nullptr || ref->header.is_white();
    });
    active_refs_.erase(it, active_refs_.end());

    return cleared_this_cycle;
}

void WeakReferenceManager::update_pointers_after_compaction(const std::function<ObjHeader*(ObjHeader*)>& forward_lookup) {
    for (ObjWeakRef* ref : active_refs_) {
        if (ref && ref->referent) {
            ref->referent = forward_lookup(ref->referent);
        }
    }
}

} // namespace bvm
