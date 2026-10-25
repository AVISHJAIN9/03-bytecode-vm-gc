#ifndef BYTECODE_VM_OBJECT_HPP
#define BYTECODE_VM_OBJECT_HPP

#include "value.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>
#include <cstring>

namespace bvm {

/// Heap Object Type Tags (Feature 16 & 28)
enum class ObjType : uint8_t {
    STRING        = 0x01,
    ARRAY         = 0x02,
    INSTANCE      = 0x03,
    NATIVE_HANDLE = 0x04,
    WEAK_REF      = 0x05
};

/// GC State Flags embedded in the object header
enum GCFlags : uint8_t {
    GC_WHITE                = 0x00,
    GC_GRAY                 = 0x01,
    GC_BLACK                = 0x02,
    GC_COLOR_MASK           = 0x03,
    GC_PINNED               = 0x04,
    GC_MARKED               = 0x08,
    GC_FINALIZER_REGISTERED = 0x10,
    GC_FINALIZER_EXECUTED   = 0x20,
    GC_OLD_GEN              = 0x40
};

/// 16-byte packed uniform Garbage Collector header (Features 16, 25, 26)
#pragma pack(push, 8)
struct ObjHeader {
    uint8_t type;         // ObjType enumeration
    uint8_t gc_flags;     // Tri-color GC flags (White/Gray/Black), Pinned, Mark, Finalizer, OldGen
    uint16_t age;         // Generational survival counter
    uint32_t size_bytes;  // Total allocated size of this object including header
    union {
        ObjHeader* next;              // Intrusive GC tracking list pointer
        ObjHeader* forwarding_address;// Used during compaction relocation (Feature 26)
    };

    ObjHeader(ObjType t, uint32_t size)
        : type(static_cast<uint8_t>(t)),
          gc_flags(GC_WHITE),
          age(0),
          size_bytes(size),
          next(nullptr) {}

    [[nodiscard]] ObjType obj_type() const noexcept {
        return static_cast<ObjType>(type);
    }

    [[nodiscard]] uint8_t color() const noexcept {
        return gc_flags & GC_COLOR_MASK;
    }

    void set_color(uint8_t c) noexcept {
        gc_flags = (gc_flags & ~GC_COLOR_MASK) | (c & GC_COLOR_MASK);
    }

    [[nodiscard]] bool is_white() const noexcept {
        return color() == GC_WHITE;
    }

    [[nodiscard]] bool is_gray() const noexcept {
        return color() == GC_GRAY;
    }

    [[nodiscard]] bool is_black() const noexcept {
        return color() == GC_BLACK;
    }

    void mark_gray() noexcept {
        set_color(GC_GRAY);
        set_marked(true);
    }

    void mark_black() noexcept {
        set_color(GC_BLACK);
        set_marked(true);
    }

    void unmark_white() noexcept {
        set_color(GC_WHITE);
        set_marked(false);
    }

    [[nodiscard]] bool is_marked() const noexcept {
        return (gc_flags & GC_MARKED) != 0;
    }

    void set_marked(bool marked) noexcept {
        if (marked) gc_flags |= GC_MARKED;
        else gc_flags &= ~GC_MARKED;
    }

    [[nodiscard]] bool is_pinned() const noexcept {
        return (gc_flags & GC_PINNED) != 0;
    }

    void set_pinned(bool pinned) noexcept {
        if (pinned) gc_flags |= GC_PINNED;
        else gc_flags &= ~GC_PINNED;
    }

    [[nodiscard]] ObjHeader* forwarding_ptr() const noexcept {
        return forwarding_address;
    }

    void set_forwarding_ptr(ObjHeader* fwd) noexcept {
        forwarding_address = fwd;
    }

    [[nodiscard]] bool is_old_gen() const noexcept {
        return (gc_flags & GC_OLD_GEN) != 0;
    }

    void set_old_gen(bool old_gen) noexcept {
        if (old_gen) gc_flags |= GC_OLD_GEN;
        else gc_flags &= ~GC_OLD_GEN;
    }

    [[nodiscard]] bool has_finalizer() const noexcept {
        return (gc_flags & GC_FINALIZER_REGISTERED) != 0;
    }

    void set_finalizer_registered(bool reg) noexcept {
        if (reg) gc_flags |= GC_FINALIZER_REGISTERED;
        else gc_flags &= ~GC_FINALIZER_REGISTERED;
    }

    [[nodiscard]] bool finalizer_executed() const noexcept {
        return (gc_flags & GC_FINALIZER_EXECUTED) != 0;
    }

    void set_finalizer_executed(bool exec) noexcept {
        if (exec) gc_flags |= GC_FINALIZER_EXECUTED;
        else gc_flags &= ~GC_FINALIZER_EXECUTED;
    }
};
#pragma pack(pop)

static_assert(sizeof(ObjHeader) == 16, "ObjHeader must be exactly 16 bytes");

/// Heap-allocated String Object with precomputed FNV-1a hash
class ObjString {
public:
    ObjHeader header;
    uint32_t length;
    uint32_t hash;
    // Flexible char array follows in allocated memory slab
    char chars[1];

    static uint32_t compute_hash(const char* key, size_t len);
    [[nodiscard]] std::string_view view() const noexcept {
        return std::string_view(chars, length);
    }
};

/// Heap-allocated Dynamic Array of Values
class ObjArray {
public:
    ObjHeader header;
    std::vector<Value> elements;

    explicit ObjArray(size_t initial_capacity = 0)
        : header(ObjType::ARRAY, sizeof(ObjArray)) {
        elements.reserve(initial_capacity);
    }
};

/// Heap-allocated Instance Object with field dictionary
class ObjInstance {
public:
    ObjHeader header;
    std::string class_name;
    std::vector<std::pair<std::string, Value>> fields;

    explicit ObjInstance(std::string name)
        : header(ObjType::INSTANCE, sizeof(ObjInstance)),
          class_name(std::move(name)) {}

    void set_field(const std::string& name, Value val);
    [[nodiscard]] const Value* get_field(const std::string& name) const;
};

/// Safe Opaque Native Handle wrapper managed on heap (Feature 13 & 16)
class ObjNativeHandle {
public:
    ObjHeader header;
    void* handle_ptr{nullptr};
    std::string type_name;
    void (*cleanup_fn)(void*){nullptr};

    ObjNativeHandle(void* ptr, std::string type, void (*cleanup)(void*) = nullptr)
        : header(ObjType::NATIVE_HANDLE, sizeof(ObjNativeHandle)),
          handle_ptr(ptr),
          type_name(std::move(type)),
          cleanup_fn(cleanup) {}

    ~ObjNativeHandle() {
        if (cleanup_fn && handle_ptr) {
            cleanup_fn(handle_ptr);
            handle_ptr = nullptr;
        }
    }
};

/// Heap-allocated Weak Reference Object (Feature 28)
class ObjWeakRef {
public:
    ObjHeader header;
    ObjHeader* referent{nullptr};

    explicit ObjWeakRef(ObjHeader* target = nullptr)
        : header(ObjType::WEAK_REF, sizeof(ObjWeakRef)),
          referent(target) {}

    [[nodiscard]] ObjHeader* get() const noexcept {
        return referent;
    }

    void set(ObjHeader* target) noexcept {
        referent = target;
    }

    [[nodiscard]] bool is_cleared() const noexcept {
        return referent == nullptr;
    }
};

/// Visitor callback type for traversing reachable child pointers in heap objects
using ObjectVisitor = std::function<void(ObjHeader*)>;

/// Visits all outgoing heap object references from `obj`
void visit_children(ObjHeader* obj, const ObjectVisitor& visitor);

} // namespace bvm

#endif // BYTECODE_VM_OBJECT_HPP
