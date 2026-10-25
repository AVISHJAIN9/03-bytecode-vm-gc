#include "object.hpp"
#include <iostream>

namespace bvm {

uint32_t ObjString::compute_hash(const char* key, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint8_t>(key[i]);
        hash *= 16777619u;
    }
    return hash;
}

void ObjInstance::set_field(const std::string& name, Value val) {
    for (auto& field : fields) {
        if (field.first == name) {
            field.second = std::move(val);
            return;
        }
    }
    fields.emplace_back(name, std::move(val));
}

const Value* ObjInstance::get_field(const std::string& name) const {
    for (const auto& field : fields) {
        if (field.first == name) {
            return &field.second;
        }
    }
    return nullptr;
}

void visit_children(ObjHeader* obj, const ObjectVisitor& visitor) {
    if (!obj) return;

    switch (obj->obj_type()) {
        case ObjType::STRING:
        case ObjType::NATIVE_HANDLE:
        case ObjType::WEAK_REF:
            // Leaf objects or weak references (weak references do not strongly retain referent)
            break;

        case ObjType::ARRAY: {
            auto* arr = reinterpret_cast<ObjArray*>(obj);
            for (auto& elem : arr->elements) {
                if (elem.is_object() && elem.as_object() != nullptr) {
                    visitor(elem.as_object());
                }
            }
            break;
        }

        case ObjType::INSTANCE: {
            auto* inst = reinterpret_cast<ObjInstance*>(obj);
            for (auto& field : inst->fields) {
                if (field.second.is_object() && field.second.as_object() != nullptr) {
                    visitor(field.second.as_object());
                }
            }
            break;
        }
    }
}

} // namespace bvm
