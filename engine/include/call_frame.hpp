#ifndef BYTECODE_VM_CALL_FRAME_HPP
#define BYTECODE_VM_CALL_FRAME_HPP

#include "closure.hpp"
#include <cstddef>
#include <memory>

namespace bvm {

/// Maximum call depth allowed before triggering stack overflow
inline constexpr size_t MAX_CALL_FRAMES = 2048;

/// Contiguous evaluation stack limit
inline constexpr size_t MAX_STACK_SIZE = 16384;

/// Call frame representing an active function activation record on the call stack (Feature 7)
struct CallFrame {
    std::shared_ptr<ObjClosure> closure; // Active closure being executed
    size_t ip{0};                        // Return/current instruction pointer in closure chunk
    size_t slot_offset{0};               // Base index in VM operand stack for this frame's window

    CallFrame() = default;

    CallFrame(std::shared_ptr<ObjClosure> cl, size_t instruction_pointer, size_t stack_base)
        : closure(std::move(cl)), ip(instruction_pointer), slot_offset(stack_base) {}

    [[nodiscard]] const Chunk& chunk() const noexcept {
        return closure->function->chunk;
    }
};

} // namespace bvm

#endif // BYTECODE_VM_CALL_FRAME_HPP
