#ifndef BYTECODE_VM_HANDLES_HPP
#define BYTECODE_VM_HANDLES_HPP

#include "object.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <functional>

namespace bvm {

/// Handle Type Enumeration (Feature 27)
enum class HandleType : uint8_t {
    STRONG = 0, // Root-retained: protects referent from GC collection
    WEAK   = 1, // Non-retaining: cleared to nullptr if referent is swept
    PINNED = 2  // Pinned & retained: immune to physical memory relocation
};

using HandleId = uint32_t;
static constexpr HandleId INVALID_HANDLE = 0xFFFFFFFFu;

/// Internal slot structure for handle table indirection
struct HandleSlot {
    ObjHeader* target{nullptr};
    uint16_t generation{1};
    uint8_t type{static_cast<uint8_t>(HandleType::STRONG)};
    uint8_t in_use{0};
    uint32_t next_free{0};
};

/// High-Performance Indirect Handle Table (Feature 27)
/// Provides stable 32-bit handle IDs surviving moving, generational, and compacting GC cycles
class HandleTable {
private:
    std::vector<HandleSlot> slots_;
    uint32_t free_head_{0xFFFFFFFFu};
    size_t active_count_{0};

    [[nodiscard]] static uint32_t slot_index(HandleId id) noexcept {
        return id & 0x0000FFFFu;
    }

    [[nodiscard]] static uint16_t handle_generation(HandleId id) noexcept {
        return static_cast<uint16_t>((id >> 16) & 0x0000FFFFu);
    }

    [[nodiscard]] static HandleId make_id(uint32_t index, uint16_t gen) noexcept {
        return (static_cast<uint32_t>(gen) << 16) | (index & 0x0000FFFFu);
    }

public:
    HandleTable();
    ~HandleTable() = default;

    HandleTable(const HandleTable&) = delete;
    HandleTable& operator=(const HandleTable&) = delete;

    /// Allocates an indirect handle of specified type
    HandleId allocate(ObjHeader* obj, HandleType type = HandleType::STRONG);

    /// Releases a previously allocated handle ID
    void release(HandleId id);

    /// Resolves handle ID to direct object pointer (or nullptr if invalid/cleared)
    [[nodiscard]] ObjHeader* resolve(HandleId id) const noexcept;

    /// Updates the object target pointed to by this handle
    void update_target(HandleId id, ObjHeader* new_obj);

    /// Validates whether a handle ID is active and valid
    [[nodiscard]] bool is_valid(HandleId id) const noexcept;

    /// Enumerates all strong and pinned handle targets into GC root set
    void enumerate_roots(std::vector<ObjHeader*>& roots) const;

    /// Updates handle slot targets to their forwarded addresses after compaction (Feature 26 & 27)
    void update_pointers_after_compaction(const std::function<ObjHeader*(ObjHeader*)>& forward_lookup);

    /// Clears targets of unvisited weak handles to nullptr
    void clear_dead_weak_handles();

    [[nodiscard]] size_t active_count() const noexcept { return active_count_; }
    [[nodiscard]] size_t strong_count() const noexcept;
    [[nodiscard]] size_t weak_count() const noexcept;
    [[nodiscard]] size_t pinned_count() const noexcept;
};

} // namespace bvm

#endif // BYTECODE_VM_HANDLES_HPP
