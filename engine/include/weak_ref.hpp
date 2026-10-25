#ifndef BYTECODE_VM_WEAK_REF_HPP
#define BYTECODE_VM_WEAK_REF_HPP

#include "object.hpp"
#include <vector>
#include <functional>
#include <cstddef>

namespace bvm {

/// Weak Reference Lifecycle & Clearing Manager (Feature 28)
class WeakReferenceManager {
private:
    std::vector<ObjWeakRef*> active_refs_;
    size_t total_cleared_{0};

public:
    WeakReferenceManager() = default;
    ~WeakReferenceManager() = default;

    WeakReferenceManager(const WeakReferenceManager&) = delete;
    WeakReferenceManager& operator=(const WeakReferenceManager&) = delete;

    /// Registers a newly created weak reference object
    void register_weak_ref(ObjWeakRef* ref);

    /// Unregisters a weak reference object
    void unregister_weak_ref(ObjWeakRef* ref);

    /// Post-marking pass: nullifies referents of weak references if referent is White (unmarked)
    size_t clear_dead_references();

    /// Updates referents to forwarded addresses during compacting GC
    void update_pointers_after_compaction(const std::function<ObjHeader*(ObjHeader*)>& forward_lookup);

    [[nodiscard]] size_t active_count() const noexcept { return active_refs_.size(); }
    [[nodiscard]] size_t total_cleared() const noexcept { return total_cleared_; }
    void clear() noexcept { active_refs_.clear(); total_cleared_ = 0; }
};

} // namespace bvm

#endif // BYTECODE_VM_WEAK_REF_HPP
