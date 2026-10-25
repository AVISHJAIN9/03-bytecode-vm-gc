#ifndef BYTECODE_VM_GENERATIONAL_HPP
#define BYTECODE_VM_GENERATIONAL_HPP

#include "object.hpp"
#include "card_table.hpp"
#include "gc_types.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <chrono>
#include <functional>

namespace bvm {

class VirtualMachine;

/// Fast Bump-Allocated Nursery Space (Eden) for Short-Lived Objects (Feature 25)
class NurserySpace {
private:
    uint8_t* memory_{nullptr};
    size_t capacity_{0};
    size_t bump_offset_{0};
    std::vector<ObjHeader*> allocated_objects_;

public:
    explicit NurserySpace(size_t capacity = 131072); // 128 KB
    ~NurserySpace();

    NurserySpace(const NurserySpace&) = delete;
    NurserySpace& operator=(const NurserySpace&) = delete;

    /// Fast bump allocation with 8-byte alignment
    [[nodiscard]] void* allocate(size_t size) noexcept;

    /// Checks if a pointer falls within nursery memory bounds
    [[nodiscard]] bool contains(const void* ptr) const noexcept {
        auto addr = reinterpret_cast<uintptr_t>(ptr);
        auto base = reinterpret_cast<uintptr_t>(memory_);
        return addr >= base && addr < (base + capacity_);
    }

    /// Resets nursery bump allocation offset in O(1)
    void reset() noexcept;

    [[nodiscard]] uint8_t* base() const noexcept { return memory_; }
    void set_bump_offset(size_t off) noexcept { bump_offset_ = off; }

    [[nodiscard]] size_t used_bytes() const noexcept { return bump_offset_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] double utilization() const noexcept {
        return capacity_ > 0 ? static_cast<double>(bump_offset_) / static_cast<double>(capacity_) : 0.0;
    }

    [[nodiscard]] const std::vector<ObjHeader*>& allocated_objects() const noexcept {
        return allocated_objects_;
    }

    [[nodiscard]] std::vector<ObjHeader*>& allocated_objects() noexcept {
        return allocated_objects_;
    }

    void register_object(ObjHeader* obj) {
        if (obj) {
            allocated_objects_.push_back(obj);
        }
    }
};

/// High-Performance Generational Garbage Collector (Feature 25)
class GenerationalCollector {
public:
    static constexpr uint16_t DEFAULT_TENURING_THRESHOLD = 3;

private:
    NurserySpace nursery_;
    CardTable card_table_;
    uint16_t tenuring_threshold_{DEFAULT_TENURING_THRESHOLD};

    std::vector<ObjHeader*> tenured_objects_;
    size_t total_minor_collections_{0};
    size_t total_promoted_objects_{0};
    size_t total_promoted_bytes_{0};
    uint64_t last_minor_pause_ns_{0};

public:
    explicit GenerationalCollector(size_t nursery_capacity = 131072);
    ~GenerationalCollector() = default;

    [[nodiscard]] NurserySpace& nursery() noexcept { return nursery_; }
    [[nodiscard]] const NurserySpace& nursery() const noexcept { return nursery_; }

    [[nodiscard]] CardTable& card_table() noexcept { return card_table_; }
    [[nodiscard]] const CardTable& card_table() const noexcept { return card_table_; }

    [[nodiscard]] uint16_t tenuring_threshold() const noexcept { return tenuring_threshold_; }
    void set_tenuring_threshold(uint16_t th) noexcept { tenuring_threshold_ = th; }

    [[nodiscard]] const std::vector<ObjHeader*>& tenured_objects() const noexcept { return tenured_objects_; }
    [[nodiscard]] std::vector<ObjHeader*>& tenured_objects() noexcept { return tenured_objects_; }

    [[nodiscard]] size_t total_minor_collections() const noexcept { return total_minor_collections_; }
    [[nodiscard]] size_t total_promoted_objects() const noexcept { return total_promoted_objects_; }
    [[nodiscard]] size_t total_promoted_bytes() const noexcept { return total_promoted_bytes_; }
    [[nodiscard]] uint64_t last_minor_pause_ns() const noexcept { return last_minor_pause_ns_; }

    /// Executes a Minor Garbage Collection targeting young objects in Eden
    /// Uses card table remembered set to avoid scanning the entire Old generation
    size_t collect_minor(
        VirtualMachine& vm,
        const std::vector<ObjHeader*>& external_roots = {}
    );

    /// Helper to register an object directly into tenured space
    void register_tenured(ObjHeader* obj) {
        if (obj) {
            obj->set_old_gen(true);
            tenured_objects_.push_back(obj);
        }
    }
};

} // namespace bvm

#endif // BYTECODE_VM_GENERATIONAL_HPP
