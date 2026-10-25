#include "generational.hpp"
#include "vm.hpp"
#include <unordered_set>
#include <chrono>

namespace bvm {

NurserySpace::NurserySpace(size_t capacity)
    : capacity_(capacity) {
    memory_ = static_cast<uint8_t*>(::operator new[](capacity_, std::align_val_t(8)));
    allocated_objects_.reserve(1024);
}

NurserySpace::~NurserySpace() {
    if (memory_) {
        ::operator delete[](memory_, std::align_val_t(8));
        memory_ = nullptr;
    }
}

void* NurserySpace::allocate(size_t size) noexcept {
    if (size == 0) size = 8;
    size_t aligned = (size + 7) & ~static_cast<size_t>(7);
    if (bump_offset_ + aligned > capacity_) {
        return nullptr; // Nursery exhausted
    }
    void* ptr = memory_ + bump_offset_;
    bump_offset_ += aligned;
    return ptr;
}

void NurserySpace::reset() noexcept {
    bump_offset_ = 0;
    allocated_objects_.clear();
}

GenerationalCollector::GenerationalCollector(size_t nursery_capacity)
    : nursery_(nursery_capacity),
      card_table_(0, 0) {
    tenured_objects_.reserve(4096);
}

size_t GenerationalCollector::collect_minor(
    VirtualMachine& vm,
    const std::vector<ObjHeader*>& external_roots
) {
    auto start_time = std::chrono::steady_clock::now();
    total_minor_collections_++;

    std::unordered_set<ObjHeader*> marked_young;
    std::vector<ObjHeader*> worklist;

    auto add_young_root = [&](ObjHeader* obj) {
        if (!obj) return;
        if (nursery_.contains(obj) && marked_young.find(obj) == marked_young.end()) {
            marked_young.insert(obj);
            worklist.push_back(obj);
        }
    };

    auto check_val = [&](const Value& val) {
        if (val.is_object() && val.as_object() != nullptr) {
            add_young_root(val.as_object());
        }
    };

    // 1. External explicit roots
    for (ObjHeader* root : external_roots) {
        add_young_root(root);
    }

    // 2. VM Stack roots
    for (const auto& val : vm.stack()) {
        check_val(val);
    }

    // 3. VM Globals roots
    for (const auto& [name, val] : vm.globals()) {
        (void)name;
        check_val(val);
    }

    // 4. Open upvalues
    auto curr_up = vm.open_upvalues();
    while (curr_up) {
        if (curr_up->location) check_val(*curr_up->location);
        if (curr_up->is_closed()) check_val(curr_up->closed);
        curr_up = curr_up->next;
    }

    // 5. Active Handles (Strong and Pinned)
    vm.handle_table().enumerate_roots(worklist);
    for (size_t i = 0; i < worklist.size(); ++i) {
        if (nursery_.contains(worklist[i])) {
            marked_young.insert(worklist[i]);
        }
    }

    // 6. Card Table Scan: find old objects that point into Nursery
    for (ObjHeader* old_obj : tenured_objects_) {
        if (old_obj && card_table_.is_dirty(old_obj)) {
            visit_children(old_obj, [&](ObjHeader* child) {
                if (child && nursery_.contains(child)) {
                    add_young_root(child);
                }
            });
        }
    }

    // 7. Transitive marking of young objects
    size_t work_idx = 0;
    while (work_idx < worklist.size()) {
        ObjHeader* curr = worklist[work_idx++];
        if (!curr || !nursery_.contains(curr)) continue;

        visit_children(curr, [&](ObjHeader* child) {
            if (child && nursery_.contains(child)) {
                if (marked_young.find(child) == marked_young.end()) {
                    marked_young.insert(child);
                    worklist.push_back(child);
                }
            }
        });
    }

    // 8. Process Nursery survivors and promotions
    size_t bytes_reclaimed = 0;
    std::vector<ObjHeader*> remaining_survivors;
    remaining_survivors.reserve(nursery_.allocated_objects().size());

    for (ObjHeader* obj : nursery_.allocated_objects()) {
        if (!obj) continue;

        if (marked_young.find(obj) != marked_young.end()) {
            // Surviving young object
            obj->age++;
            if (obj->age >= tenuring_threshold_) {
                // Promote to Tenured space
                obj->set_old_gen(true);
                obj->unmark_white();
                tenured_objects_.push_back(obj);
                total_promoted_objects_++;
                total_promoted_bytes_ += obj->size_bytes;
            } else {
                obj->unmark_white();
                remaining_survivors.push_back(obj);
            }
        } else {
            // Dead young object
            bytes_reclaimed += obj->size_bytes;
        }
    }

    // 9. Reclaim Nursery Eden memory: compact remaining young survivors to base of Eden
    uint8_t* cursor = nursery_.base();
    for (ObjHeader*& obj : remaining_survivors) {
        size_t aligned = (obj->size_bytes + 7) & ~static_cast<size_t>(7);
        auto* dest = reinterpret_cast<ObjHeader*>(cursor);
        if (dest != obj) {
            std::memmove(dest, obj, obj->size_bytes);
            for (auto*& r : const_cast<std::vector<ObjHeader*>&>(external_roots)) {
                if (r == obj) r = dest;
            }
            obj = dest;
        }
        cursor += aligned;
    }
    nursery_.set_bump_offset(cursor - nursery_.base());
    nursery_.allocated_objects() = std::move(remaining_survivors);

    // 10. Clean scanned dirty cards
    card_table_.clear_all();

    auto end_time = std::chrono::steady_clock::now();
    last_minor_pause_ns_ = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count()
    );

    return bytes_reclaimed;
}

} // namespace bvm
