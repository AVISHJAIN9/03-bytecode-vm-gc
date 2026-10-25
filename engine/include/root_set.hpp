#ifndef BYTECODE_VM_ROOT_SET_HPP
#define BYTECODE_VM_ROOT_SET_HPP

#include "object.hpp"
#include <vector>
#include <functional>

namespace bvm {

class VirtualMachine;
class NativeHandleManager;

/// Exhaustive Garbage Collector Root Set Enumerator (Feature 18)
class RootSetEnumerator {
public:
    /// Discovers and enumerates all GC root pointers across operand stacks, call frames, globals, upvalues, and native handles
    static void enumerate(
        const VirtualMachine& vm,
        const NativeHandleManager& handle_mgr,
        std::vector<ObjHeader*>& roots
    );

    /// Formatted diagnostic dump of all discovered GC roots
    [[nodiscard]] static std::string dump_root_summary(const std::vector<ObjHeader*>& roots);
};

} // namespace bvm

#endif // BYTECODE_VM_ROOT_SET_HPP
