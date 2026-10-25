#include "compact.hpp"
#include "vm.hpp"
#include "weak_ref.hpp"
#include <algorithm>
#include <cstring>
#include <chrono>

namespace bvm {

CompactingArena::CompactingArena(size_t capacity)
    : capacity_(capacity) {
    memory_ = static_cast<uint8_t*>(::operator new[](capacity_, std::align_val_t(8)));
    live_objects_.reserve(1024);
}

CompactingArena::~CompactingArena() {
    if (memory_) {
        ::operator delete[](memory_, std::align_val_t(8));
        memory_ = nullptr;
    }
}

void* CompactingArena::allocate(size_t size) noexcept {
    if (size == 0) size = 8;
    size_t aligned = (size + 7) & ~static_cast<size_t>(7);
    if (used_bytes_ + aligned > capacity_) {
        return nullptr;
    }
    void* ptr = memory_ + used_bytes_;
    used_bytes_ += aligned;
    return ptr;
}

bool CompactingArena::contains(const void* ptr) const noexcept {
    auto addr = reinterpret_cast<uintptr_t>(ptr);
    auto base = reinterpret_cast<uintptr_t>(memory_);
    return addr >= base && addr < (base + capacity_);
}

void CompactingArena::register_object(ObjHeader* obj) {
    if (obj) {
        live_objects_.push_back(obj);
    }
}

void CompactingArena::unregister_object(ObjHeader* obj) {
    if (!obj) return;
    for (auto it = live_objects_.begin(); it != live_objects_.end(); ++it) {
        if (*it == obj) {
            live_objects_.erase(it);
            break;
        }
    }
}

ObjInstance* CompactingArena::allocate_instance(const std::string& class_name) {
    size_t total_size = sizeof(ObjInstance);
    void* mem = allocate(total_size);
    if (!mem) return nullptr;

    auto* inst = new (mem) ObjInstance(class_name);
    register_object(&inst->header);
    return inst;
}

ObjArray* CompactingArena::allocate_array(size_t initial_capacity) {
    size_t total_size = sizeof(ObjArray);
    void* mem = allocate(total_size);
    if (!mem) return nullptr;

    auto* arr = new (mem) ObjArray(initial_capacity);
    register_object(&arr->header);
    return arr;
}

ObjString* CompactingArena::allocate_string(const char* chars, size_t length) {
    size_t total_size = sizeof(ObjHeader) + sizeof(uint32_t) + sizeof(uint32_t) + length + 1;
    void* mem = allocate(total_size);
    if (!mem) return nullptr;

    auto* str = static_cast<ObjString*>(mem);
    new (&str->header) ObjHeader(ObjType::STRING, static_cast<uint32_t>(total_size));
    str->length = static_cast<uint32_t>(length);
    str->hash = ObjString::compute_hash(chars, length);
    if (chars && length > 0) {
        std::memcpy(str->chars, chars, length);
    }
    str->chars[length] = '\0';

    register_object(&str->header);
    return str;
}

ObjWeakRef* CompactingArena::allocate_weak_ref(ObjHeader* target) {
    size_t total_size = sizeof(ObjWeakRef);
    void* mem = allocate(total_size);
    if (!mem) return nullptr;

    auto* ref = new (mem) ObjWeakRef(target);
    register_object(&ref->header);
    return ref;
}

Compactor::CompactionResult Compactor::compact(
    CompactingArena& arena,
    VirtualMachine& vm,
    HandleTable* handle_table,
    WeakReferenceManager* weak_mgr
) {
    auto start_time = std::chrono::steady_clock::now();
    CompactionResult result;

    auto& live = arena.live_objects();
    if (live.empty()) {
        result.contiguous_free_bytes = arena.capacity();
        return result;
    }

    // Sort live objects in memory order (low to high address) for sliding compaction
    std::sort(live.begin(), live.end(), [](ObjHeader* a, ObjHeader* b) {
        return reinterpret_cast<uintptr_t>(a) < reinterpret_cast<uintptr_t>(b);
    });

    // -------------------------------------------------------------
    // PASS 1: Calculate Forwarding Destination Addresses
    // -------------------------------------------------------------
    uint8_t* free_cursor = arena.base();
    for (ObjHeader* obj : live) {
        size_t aligned_sz = (obj->size_bytes + 7) & ~static_cast<size_t>(7);
        if (obj->is_pinned()) {
            // Pinned objects cannot be relocated physically
            obj->set_forwarding_ptr(obj);
            free_cursor = std::max(free_cursor, reinterpret_cast<uint8_t*>(obj) + aligned_sz);
            result.objects_pinned++;
        } else {
            auto* target = reinterpret_cast<ObjHeader*>(free_cursor);
            obj->set_forwarding_ptr(target);
            if (target != obj) {
                result.objects_moved++;
            }
            free_cursor += aligned_sz;
        }
    }

    // -------------------------------------------------------------
    // PASS 2: Update all live references across the entire VM
    // -------------------------------------------------------------
    auto resolve_fwd = [&](ObjHeader* ptr) -> ObjHeader* {
        if (ptr && arena.contains(ptr) && ptr->forwarding_ptr() != nullptr) {
            return ptr->forwarding_ptr();
        }
        return ptr;
    };

    auto update_val = [&](Value& v) {
        if (v.is_object() && v.as_object() != nullptr) {
            v = Value(resolve_fwd(v.as_object()));
        }
    };

    // 1. VM stack
    for (auto& v : const_cast<std::vector<Value>&>(vm.stack())) {
        update_val(v);
    }

    // 2. VM globals
    for (auto& [name, v] : const_cast<std::unordered_map<std::string, Value>&>(vm.globals())) {
        (void)name;
        update_val(v);
    }

    // 3. VM open upvalues
    auto up = vm.open_upvalues();
    while (up) {
        if (up->location) update_val(*up->location);
        if (up->is_closed()) update_val(up->closed);
        up = up->next;
    }

    // 4. Handle table
    if (handle_table) {
        handle_table->update_pointers_after_compaction(resolve_fwd);
    }

    // 5. Weak reference manager
    if (weak_mgr) {
        weak_mgr->update_pointers_after_compaction(resolve_fwd);
    }

    // 6. Internal references inside all live objects in the arena
    for (ObjHeader* obj : live) {
        switch (obj->obj_type()) {
            case ObjType::STRING:
            case ObjType::NATIVE_HANDLE:
                break;
            case ObjType::ARRAY: {
                auto* arr = reinterpret_cast<ObjArray*>(obj);
                for (auto& elem : arr->elements) {
                    update_val(elem);
                }
                break;
            }
            case ObjType::INSTANCE: {
                auto* inst = reinterpret_cast<ObjInstance*>(obj);
                for (auto& field : inst->fields) {
                    update_val(field.second);
                }
                break;
            }
            case ObjType::WEAK_REF: {
                auto* wr = reinterpret_cast<ObjWeakRef*>(obj);
                if (wr->referent) {
                    wr->referent = resolve_fwd(wr->referent);
                }
                break;
            }
        }
    }

    // -------------------------------------------------------------
    // PASS 3: Physically Relocate Memory Payloads
    // -------------------------------------------------------------
    for (ObjHeader* obj : live) {
        ObjHeader* dest = obj->forwarding_ptr();
        size_t sz = obj->size_bytes;
        if (dest != obj) {
            std::memmove(dest, obj, sz);
        }
    }

    // Update live objects list to point to new relocated memory addresses
    for (auto& obj : live) {
        obj = obj->forwarding_ptr();
    }

    size_t new_used_bytes = static_cast<size_t>(free_cursor - arena.base());
    size_t old_used_bytes = arena.used_bytes();
    if (new_used_bytes < old_used_bytes) {
        std::memset(arena.base() + new_used_bytes, 0, old_used_bytes - new_used_bytes);
        result.bytes_compacted = old_used_bytes - new_used_bytes;
    }
    arena.set_used_bytes(new_used_bytes);
    result.contiguous_free_bytes = arena.capacity() - new_used_bytes;

    auto end_time = std::chrono::steady_clock::now();
    result.pause_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count()
    );

    return result;
}

} // namespace bvm
