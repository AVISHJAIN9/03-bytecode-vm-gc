#ifndef BYTECODE_VM_ALLOCATOR_HPP
#define BYTECODE_VM_ALLOCATOR_HPP

#include "object.hpp"
#include "resource_limits.hpp"
#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>
#include <string_view>
#include <memory>
#include <new>

namespace bvm {

/// Segregated Size-Class Slab Allocator and Heap Object Manager (Feature 17)
class MemoryAllocator {
public:
    static constexpr size_t NUM_SIZE_CLASSES = 5;
    static constexpr std::array<size_t, NUM_SIZE_CLASSES> SIZE_CLASSES = {32, 64, 128, 256, 512};
    static constexpr size_t SLAB_SIZE = 16384; // 16 KB slab per size class chunk

private:
    struct FreeNode {
        FreeNode* next;
    };

    struct Slab {
        uint8_t* memory{nullptr};
        Slab* next{nullptr};
    };

    std::array<FreeNode*, NUM_SIZE_CLASSES> free_lists_{};
    std::array<Slab*, NUM_SIZE_CLASSES> slabs_{};
    std::vector<std::pair<void*, size_t>> large_allocations_;

    ResourceQuota* quota_{nullptr};
    ObjHeader* all_objects_{nullptr};
    size_t total_bytes_allocated_{0};

    [[nodiscard]] int get_size_class_index(size_t size) const noexcept;
    void allocate_new_slab(size_t class_idx);

public:
    explicit MemoryAllocator(ResourceQuota* quota = nullptr);
    ~MemoryAllocator();

    MemoryAllocator(const MemoryAllocator&) = delete;
    MemoryAllocator& operator=(const MemoryAllocator&) = delete;

    void set_quota(ResourceQuota* quota) noexcept { quota_ = quota; }

    /// Raw memory allocation with size-class binning and 8-byte alignment
    [[nodiscard]] void* allocate(size_t size);

    /// Raw memory deallocation returning memory to segregated free list
    void deallocate(void* ptr, size_t size) noexcept;

    /// Typed heap object allocators
    ObjString* allocate_string(const char* chars, size_t length);
    ObjString* allocate_string(std::string_view sv) {
        return allocate_string(sv.data(), sv.size());
    }

    ObjArray* allocate_array(size_t initial_capacity = 0);
    ObjInstance* allocate_instance(const std::string& class_name);
    ObjNativeHandle* allocate_native_handle(void* ptr, const std::string& type, void (*cleanup)(void*) = nullptr);
    ObjWeakRef* allocate_weak_ref(ObjHeader* target = nullptr);

    /// Destroys and deallocates a single managed heap object
    void free_object(ObjHeader* obj);

    /// Destroys and deallocates all managed heap objects
    void free_all();

    [[nodiscard]] ObjHeader* all_objects() const noexcept { return all_objects_; }
    [[nodiscard]] ObjHeader** all_objects_ptr() noexcept { return &all_objects_; }
    [[nodiscard]] size_t total_allocated_bytes() const noexcept { return total_bytes_allocated_; }
};

} // namespace bvm

#endif // BYTECODE_VM_ALLOCATOR_HPP
