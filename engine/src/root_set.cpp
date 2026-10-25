#include "root_set.hpp"
#include "vm.hpp"
#include "ffi.hpp"
#include <unordered_set>
#include <sstream>
#include <iomanip>

namespace bvm {

void RootSetEnumerator::enumerate(
    const VirtualMachine& vm,
    const NativeHandleManager& handle_mgr,
    std::vector<ObjHeader*>& roots
) {
    roots.clear();
    std::unordered_set<ObjHeader*> seen;

    auto add_root = [&](ObjHeader* obj) {
        if (obj && seen.find(obj) == seen.end()) {
            seen.insert(obj);
            roots.push_back(obj);
        }
    };

    auto check_val = [&](const Value& val) {
        if (val.is_object() && val.as_object() != nullptr) {
            add_root(val.as_object());
        }
    };

    // 1. Scan operand stack values
    for (const auto& val : vm.stack()) {
        check_val(val);
    }

    // 2. Scan global variables
    for (const auto& [name, val] : vm.globals()) {
        (void)name;
        check_val(val);
    }

    // 3. Scan open upvalues
    auto curr_up = vm.open_upvalues();
    while (curr_up) {
        if (curr_up->location) {
            check_val(*curr_up->location);
        }
        if (curr_up->is_closed()) {
            check_val(curr_up->closed);
        }
        curr_up = curr_up->next;
    }

    // 4. Scan active native handles
    for (ObjNativeHandle* handle : handle_mgr.active_handles()) {
        if (handle) {
            add_root(&handle->header);
        }
    }

    // 5. Scan active strong and pinned handles from HandleTable (Feature 27)
    std::vector<ObjHeader*> handle_roots;
    vm.handle_table().enumerate_roots(handle_roots);
    for (ObjHeader* hr : handle_roots) {
        add_root(hr);
    }
}

std::string RootSetEnumerator::dump_root_summary(const std::vector<ObjHeader*>& roots) {
    std::ostringstream ss;
    ss << "GC Root Set Enumeration (" << roots.size() << " roots discovered):\n";
    for (size_t i = 0; i < roots.size(); ++i) {
        ObjHeader* obj = roots[i];
        ss << "  #" << std::setw(3) << i << " [" << std::hex << static_cast<const void*>(obj) << std::dec << "] ";
        switch (obj->obj_type()) {
            case ObjType::STRING: {
                auto* s = reinterpret_cast<ObjString*>(obj);
                ss << "String: \"" << s->view() << "\"\n";
                break;
            }
            case ObjType::ARRAY: {
                auto* a = reinterpret_cast<ObjArray*>(obj);
                ss << "Array (" << a->elements.size() << " elements)\n";
                break;
            }
            case ObjType::INSTANCE: {
                auto* inst = reinterpret_cast<ObjInstance*>(obj);
                ss << "Instance of " << inst->class_name << " (" << inst->fields.size() << " fields)\n";
                break;
            }
            case ObjType::NATIVE_HANDLE: {
                auto* h = reinterpret_cast<ObjNativeHandle*>(obj);
                ss << "NativeHandle [" << h->type_name << "]\n";
                break;
            }
            case ObjType::WEAK_REF: {
                auto* w = reinterpret_cast<ObjWeakRef*>(obj);
                ss << "WeakRef [referent=" << static_cast<const void*>(w->referent) << "]\n";
                break;
            }
        }
    }
    return ss.str();
}

} // namespace bvm
