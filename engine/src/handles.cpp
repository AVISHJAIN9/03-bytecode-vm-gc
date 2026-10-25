#include "handles.hpp"
#include <algorithm>

namespace bvm {

HandleTable::HandleTable() {
    slots_.reserve(256);
}

HandleId HandleTable::allocate(ObjHeader* obj, HandleType type) {
    uint32_t index = 0;
    if (free_head_ != 0xFFFFFFFFu) {
        index = free_head_;
        free_head_ = slots_[index].next_free;
    } else {
        index = static_cast<uint32_t>(slots_.size());
        slots_.emplace_back();
    }

    auto& slot = slots_[index];
    slot.target = obj;
    slot.type = static_cast<uint8_t>(type);
    slot.in_use = 1;
    active_count_++;

    if (type == HandleType::PINNED && obj) {
        obj->set_pinned(true);
    }

    return make_id(index, slot.generation);
}

void HandleTable::release(HandleId id) {
    uint32_t index = slot_index(id);
    uint16_t gen = handle_generation(id);

    if (index >= slots_.size()) return;
    auto& slot = slots_[index];
    if (!slot.in_use || slot.generation != gen) return;

    if (slot.type == static_cast<uint8_t>(HandleType::PINNED) && slot.target) {
        // Check if other pinned handles still reference this object
        bool still_pinned = false;
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (i != index && slots_[i].in_use &&
                slots_[i].type == static_cast<uint8_t>(HandleType::PINNED) &&
                slots_[i].target == slot.target) {
                still_pinned = true;
                break;
            }
        }
        if (!still_pinned) {
            slot.target->set_pinned(false);
        }
    }

    slot.target = nullptr;
    slot.in_use = 0;
    slot.generation++; // Invalidate stale handle IDs
    if (slot.generation == 0) slot.generation = 1;

    slot.next_free = free_head_;
    free_head_ = index;
    active_count_--;
}

ObjHeader* HandleTable::resolve(HandleId id) const noexcept {
    uint32_t index = slot_index(id);
    uint16_t gen = handle_generation(id);

    if (index >= slots_.size()) return nullptr;
    const auto& slot = slots_[index];
    if (!slot.in_use || slot.generation != gen) return nullptr;
    return slot.target;
}

void HandleTable::update_target(HandleId id, ObjHeader* new_obj) {
    uint32_t index = slot_index(id);
    uint16_t gen = handle_generation(id);

    if (index >= slots_.size()) return;
    auto& slot = slots_[index];
    if (!slot.in_use || slot.generation != gen) return;

    if (slot.type == static_cast<uint8_t>(HandleType::PINNED)) {
        if (slot.target) slot.target->set_pinned(false);
        if (new_obj) new_obj->set_pinned(true);
    }
    slot.target = new_obj;
}

bool HandleTable::is_valid(HandleId id) const noexcept {
    return resolve(id) != nullptr;
}

void HandleTable::enumerate_roots(std::vector<ObjHeader*>& roots) const {
    for (const auto& slot : slots_) {
        if (slot.in_use && slot.target != nullptr) {
            if (slot.type == static_cast<uint8_t>(HandleType::STRONG) ||
                slot.type == static_cast<uint8_t>(HandleType::PINNED)) {
                roots.push_back(slot.target);
            }
        }
    }
}

void HandleTable::update_pointers_after_compaction(const std::function<ObjHeader*(ObjHeader*)>& forward_lookup) {
    for (auto& slot : slots_) {
        if (slot.in_use && slot.target != nullptr) {
            slot.target = forward_lookup(slot.target);
        }
    }
}

void HandleTable::clear_dead_weak_handles() {
    for (auto& slot : slots_) {
        if (slot.in_use && slot.type == static_cast<uint8_t>(HandleType::WEAK) && slot.target != nullptr) {
            if (slot.target->is_white()) {
                slot.target = nullptr;
            }
        }
    }
}

size_t HandleTable::strong_count() const noexcept {
    size_t c = 0;
    for (const auto& s : slots_) {
        if (s.in_use && s.type == static_cast<uint8_t>(HandleType::STRONG)) c++;
    }
    return c;
}

size_t HandleTable::weak_count() const noexcept {
    size_t c = 0;
    for (const auto& s : slots_) {
        if (s.in_use && s.type == static_cast<uint8_t>(HandleType::WEAK)) c++;
    }
    return c;
}

size_t HandleTable::pinned_count() const noexcept {
    size_t c = 0;
    for (const auto& s : slots_) {
        if (s.in_use && s.type == static_cast<uint8_t>(HandleType::PINNED)) c++;
    }
    return c;
}

} // namespace bvm
