#include "allocator.hpp"
#include <algorithm>
#include <cassert>

namespace bvm {

MemoryAllocator::MemoryAllocator(ResourceQuota* quota)
    : quota_(quota) {
    free_lists_.fill(nullptr);
    slabs_.fill(nullptr);
}

MemoryAllocator::~MemoryAllocator() {
    free_all();

    // Free all slab buffers
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        Slab* slab = slabs_[i];
        while (slab) {
            Slab* next = slab->next;
            ::operator delete[](slab->memory, std::align_val_t(8));
            delete slab;
            slab = next;
        }
        slabs_[i] = nullptr;
        free_lists_[i] = nullptr;
    }

    // Free any remaining large allocations
    for (auto& [ptr, size] : large_allocations_) {
        (void)size;
        ::operator delete(ptr, std::align_val_t(8));
    }
    large_allocations_.clear();
}

int MemoryAllocator::get_size_class_index(size_t size) const noexcept {
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        if (size <= SIZE_CLASSES[i]) {
            return static_cast<int>(i);
        }
    }
    return -1; // Large object
}

void MemoryAllocator::allocate_new_slab(size_t class_idx) {
    size_t chunk_size = SIZE_CLASSES[class_idx];
    size_t num_chunks = SLAB_SIZE / chunk_size;

    auto* slab_mem = static_cast<uint8_t*>(::operator new[](SLAB_SIZE, std::align_val_t(8)));
    auto* slab = new Slab{slab_mem, slabs_[class_idx]};
    slabs_[class_idx] = slab;

    // Slice slab into chunk nodes and link to free list
    for (size_t i = 0; i < num_chunks; ++i) {
        auto* node = reinterpret_cast<FreeNode*>(slab_mem + (i * chunk_size));
        node->next = free_lists_[class_idx];
        free_lists_[class_idx] = node;
    }
}

void* MemoryAllocator::allocate(size_t size) {
    if (size == 0) size = 8;
    // Align requested size up to multiple of 8
    size_t aligned_size = (size + 7) & ~static_cast<size_t>(7);

    int class_idx = get_size_class_index(aligned_size);
    size_t allocated_bytes = (class_idx != -1) ? SIZE_CLASSES[class_idx] : aligned_size;

    // Enforce resource limit quota
    if (quota_) {
        quota_->track_allocation(allocated_bytes);
    }
    total_bytes_allocated_ += allocated_bytes;

    void* ptr = nullptr;
    if (class_idx != -1) {
        if (!free_lists_[class_idx]) {
            allocate_new_slab(static_cast<size_t>(class_idx));
        }
        FreeNode* node = free_lists_[class_idx];
        free_lists_[class_idx] = node->next;
        ptr = static_cast<void*>(node);
    } else {
        ptr = ::operator new(allocated_bytes, std::align_val_t(8));
        large_allocations_.emplace_back(ptr, allocated_bytes);
    }

    // Verify strict 8-byte alignment guarantee
    assert((reinterpret_cast<uintptr_t>(ptr) & 7) == 0);
    return ptr;
}

void MemoryAllocator::deallocate(void* ptr, size_t size) noexcept {
    if (!ptr) return;

    size_t aligned_size = (size + 7) & ~static_cast<size_t>(7);
    int class_idx = get_size_class_index(aligned_size);
    size_t allocated_bytes = (class_idx != -1) ? SIZE_CLASSES[class_idx] : aligned_size;

    if (quota_) {
        quota_->track_deallocation(allocated_bytes);
    }
    if (total_bytes_allocated_ >= allocated_bytes) {
        total_bytes_allocated_ -= allocated_bytes;
    } else {
        total_bytes_allocated_ = 0;
    }

    if (class_idx != -1) {
        auto* node = static_cast<FreeNode*>(ptr);
        node->next = free_lists_[class_idx];
        free_lists_[class_idx] = node;
    } else {
        auto it = std::find_if(large_allocations_.begin(), large_allocations_.end(),
            [ptr](const std::pair<void*, size_t>& item) { return item.first == ptr; });
        if (it != large_allocations_.end()) {
            large_allocations_.erase(it);
        }
        ::operator delete(ptr, std::align_val_t(8));
    }
}

ObjString* MemoryAllocator::allocate_string(const char* chars, size_t length) {
    // Total size = header + length + hash + chars + null terminator
    size_t total_size = sizeof(ObjHeader) + sizeof(uint32_t) + sizeof(uint32_t) + length + 1;
    void* mem = allocate(total_size);

    auto* str = static_cast<ObjString*>(mem);
    new (&str->header) ObjHeader(ObjType::STRING, static_cast<uint32_t>(total_size));
    str->length = static_cast<uint32_t>(length);
    str->hash = ObjString::compute_hash(chars, length);
    if (chars && length > 0) {
        std::memcpy(str->chars, chars, length);
    }
    str->chars[length] = '\0';

    // Link into global GC object chain
    str->header.next = all_objects_;
    all_objects_ = &str->header;

    return str;
}

ObjArray* MemoryAllocator::allocate_array(size_t initial_capacity) {
    size_t total_size = sizeof(ObjArray);
    void* mem = allocate(total_size);

    auto* arr = new (mem) ObjArray(initial_capacity);
    arr->header.next = all_objects_;
    all_objects_ = &arr->header;

    return arr;
}

ObjInstance* MemoryAllocator::allocate_instance(const std::string& class_name) {
    size_t total_size = sizeof(ObjInstance);
    void* mem = allocate(total_size);

    auto* inst = new (mem) ObjInstance(class_name);
    inst->header.next = all_objects_;
    all_objects_ = &inst->header;

    return inst;
}

ObjNativeHandle* MemoryAllocator::allocate_native_handle(void* ptr, const std::string& type, void (*cleanup)(void*)) {
    size_t total_size = sizeof(ObjNativeHandle);
    void* mem = allocate(total_size);

    auto* handle = new (mem) ObjNativeHandle(ptr, type, cleanup);
    handle->header.next = all_objects_;
    all_objects_ = &handle->header;

    return handle;
}

ObjWeakRef* MemoryAllocator::allocate_weak_ref(ObjHeader* target) {
    size_t total_size = sizeof(ObjWeakRef);
    void* mem = allocate(total_size);

    auto* ref = new (mem) ObjWeakRef(target);
    ref->header.next = all_objects_;
    all_objects_ = &ref->header;

    return ref;
}

void MemoryAllocator::free_object(ObjHeader* obj) {
    if (!obj) return;

    size_t size = obj->size_bytes;
    switch (obj->obj_type()) {
        case ObjType::STRING:
            // Leaf object
            break;
        case ObjType::ARRAY:
            reinterpret_cast<ObjArray*>(obj)->~ObjArray();
            break;
        case ObjType::INSTANCE:
            reinterpret_cast<ObjInstance*>(obj)->~ObjInstance();
            break;
        case ObjType::NATIVE_HANDLE:
            reinterpret_cast<ObjNativeHandle*>(obj)->~ObjNativeHandle();
            break;
        case ObjType::WEAK_REF:
            reinterpret_cast<ObjWeakRef*>(obj)->~ObjWeakRef();
            break;
    }

    deallocate(obj, size);
}

void MemoryAllocator::free_all() {
    ObjHeader* curr = all_objects_;
    while (curr) {
        ObjHeader* next = curr->next;
        free_object(curr);
        curr = next;
    }
    all_objects_ = nullptr;
}

} // namespace bvm
