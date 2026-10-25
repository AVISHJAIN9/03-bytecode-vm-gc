#include "card_table.hpp"

namespace bvm {

CardTable::CardTable(uintptr_t base_addr, size_t size) {
    initialize(base_addr, size);
}

void CardTable::initialize(uintptr_t base_addr, size_t size) {
    base_address_ = base_addr;
    memory_size_ = size;
    dirty_cards_.clear();
}

void CardTable::mark_dirty(const void* ptr) noexcept {
    if (!ptr) return;
    auto addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t card_idx = addr >> CARD_SHIFT;
    dirty_cards_.insert(card_idx);
}

void CardTable::mark_card_dirty(uintptr_t card_idx) noexcept {
    dirty_cards_.insert(card_idx);
}

bool CardTable::is_dirty(const void* ptr) const noexcept {
    if (!ptr) return false;
    auto addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t card_idx = addr >> CARD_SHIFT;
    return dirty_cards_.find(card_idx) != dirty_cards_.end();
}

bool CardTable::is_card_dirty(uintptr_t card_idx) const noexcept {
    return dirty_cards_.find(card_idx) != dirty_cards_.end();
}

void CardTable::clear_card(uintptr_t card_idx) noexcept {
    dirty_cards_.erase(card_idx);
}

void CardTable::clear_all() noexcept {
    dirty_cards_.clear();
}

size_t CardTable::dirty_count() const noexcept {
    return dirty_cards_.size();
}

uintptr_t CardTable::card_start_address(uintptr_t card_idx) const noexcept {
    return card_idx << CARD_SHIFT;
}

uintptr_t CardTable::card_end_address(uintptr_t card_idx) const noexcept {
    return (card_idx + 1) << CARD_SHIFT;
}

void CardTable::scan_dirty_cards(const std::function<void(uintptr_t card_idx, uintptr_t start_addr, uintptr_t end_addr)>& callback) {
    for (uintptr_t card_idx : dirty_cards_) {
        callback(card_idx, card_start_address(card_idx), card_end_address(card_idx));
    }
}

} // namespace bvm
