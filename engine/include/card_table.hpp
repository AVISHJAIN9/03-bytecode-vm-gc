#ifndef BYTECODE_VM_CARD_TABLE_HPP
#define BYTECODE_VM_CARD_TABLE_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_set>
#include <functional>
#include <algorithm>

namespace bvm {

/// Card Table for Generational Remembered Set Tracking (Feature 25)
/// Maps each 512-byte heap page to a card (512-byte granularity)
class CardTable {
public:
    static constexpr size_t CARD_SHIFT = 9;                   // 2^9 = 512 bytes per card
    static constexpr size_t CARD_SIZE  = 1ULL << CARD_SHIFT; // 512 bytes
    static constexpr uint8_t CARD_CLEAN = 0x00;
    static constexpr uint8_t CARD_DIRTY = 0x01;

private:
    uintptr_t base_address_{0};
    size_t memory_size_{0};
    std::unordered_set<uintptr_t> dirty_cards_;

public:
    CardTable() = default;
    CardTable(uintptr_t base_addr, size_t size);

    void initialize(uintptr_t base_addr, size_t size);

    /// Marks the card containing `ptr` as dirty (0x01)
    void mark_dirty(const void* ptr) noexcept;

    /// Marks the card at index `card_idx` as dirty
    void mark_card_dirty(uintptr_t card_idx) noexcept;

    /// Returns true if the card containing `ptr` is marked dirty
    [[nodiscard]] bool is_dirty(const void* ptr) const noexcept;

    /// Returns true if the card at index `card_idx` is marked dirty
    [[nodiscard]] bool is_card_dirty(uintptr_t card_idx) const noexcept;

    /// Clears a single card back to clean (0x00)
    void clear_card(uintptr_t card_idx) noexcept;

    /// Clears all cards across the entire card table
    void clear_all() noexcept;

    /// Counts the total number of dirty cards currently marked
    [[nodiscard]] size_t dirty_count() const noexcept;

    [[nodiscard]] size_t total_cards() const noexcept {
        return memory_size_ > 0 ? ((memory_size_ + CARD_SIZE - 1) >> CARD_SHIFT) : dirty_cards_.size();
    }
    [[nodiscard]] uintptr_t base_address() const noexcept { return base_address_; }
    [[nodiscard]] size_t memory_size() const noexcept { return memory_size_; }

    [[nodiscard]] uintptr_t card_start_address(uintptr_t card_idx) const noexcept;
    [[nodiscard]] uintptr_t card_end_address(uintptr_t card_idx) const noexcept;

    /// Iterates over all dirty cards, invoking callback(card_idx, start_addr, end_addr)
    void scan_dirty_cards(const std::function<void(uintptr_t card_idx, uintptr_t start_addr, uintptr_t end_addr)>& callback);
};

} // namespace bvm

#endif // BYTECODE_VM_CARD_TABLE_HPP
