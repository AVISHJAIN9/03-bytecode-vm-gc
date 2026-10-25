#ifndef BYTECODE_VM_COMPACT_HPP
#define BYTECODE_VM_COMPACT_HPP

#include "object.hpp"
#include "handles.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <functional>
#include <string>

namespace bvm {

class VirtualMachine;
class WeakReferenceManager;

/// Contiguous Compacting Heap Arena (Feature 26)
class CompactingArena {
private:
    uint8_t* memory_{nullptr};
    size_t capacity_{0};
    size_t used_bytes_{0};
    std::vector<ObjHeader*> live_objects_;

public:
    explicit CompactingArena(size_t capacity = 1048576); // 1 MB
    ~CompactingArena();

    CompactingArena(const CompactingArena&) = delete;
    CompactingArena& operator=(const CompactingArena&) = delete;

    [[nodiscard]] void* allocate(size_t size) noexcept;
    [[nodiscard]] bool contains(const void* ptr) const noexcept;

    [[nodiscard]] size_t used_bytes() const noexcept { return used_bytes_; }
    void set_used_bytes(size_t sz) noexcept { used_bytes_ = sz; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] uint8_t* base() const noexcept { return memory_; }

    [[nodiscard]] const std::vector<ObjHeader*>& live_objects() const noexcept { return live_objects_; }
    [[nodiscard]] std::vector<ObjHeader*>& live_objects() noexcept { return live_objects_; }

    void register_object(ObjHeader* obj);
    void unregister_object(ObjHeader* obj);

    ObjInstance* allocate_instance(const std::string& class_name);
    ObjArray* allocate_array(size_t initial_capacity = 0);
    ObjString* allocate_string(const char* chars, size_t length);
    ObjWeakRef* allocate_weak_ref(ObjHeader* target = nullptr);
};

/// Multi-Pass Sliding Compactor & Memory Relocation Engine (Feature 26)
class Compactor {
public:
    struct CompactionResult {
        size_t objects_moved{0};
        size_t objects_pinned{0};
        size_t bytes_compacted{0};
        size_t contiguous_free_bytes{0};
        uint64_t pause_ns{0};
    };

    /// Runs a 3-pass sliding compaction over the specified arena:
    /// Pass 1: Compute forwarding destinations for all unpinned live objects
    /// Pass 2: Update all heap pointers, stack slots, globals, upvalues, and handles
    /// Pass 3: Physically relocate memory payloads with std::memmove and zero reclaimed tail
    static CompactionResult compact(
        CompactingArena& arena,
        VirtualMachine& vm,
        HandleTable* handle_table = nullptr,
        WeakReferenceManager* weak_mgr = nullptr
    );
};

} // namespace bvm

#endif // BYTECODE_VM_COMPACT_HPP
