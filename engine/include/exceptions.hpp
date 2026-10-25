#ifndef BYTECODE_VM_EXCEPTIONS_HPP
#define BYTECODE_VM_EXCEPTIONS_HPP

#include "call_frame.hpp"
#include <vector>
#include <string>
#include <optional>

namespace bvm {

/// Runtime Try-Catch handler entry (Feature 9)
struct TryHandler {
    size_t frame_index;   // Call frame index where the try block was registered
    size_t handler_ip;    // Bytecode offset of the catch block in the registered frame's function
    size_t stack_depth;   // Operand stack height to restore upon catching
};

/// Exception unwinding result
struct UnwindResult {
    bool handled{false};
    size_t target_frame_index{0};
    size_t handler_ip{0};
    size_t target_stack_depth{0};
    std::string unhandled_message{};
};

/// Exception unwinder that resolves handler entries across call frames
class ExceptionUnwinder {
public:
    /// Searches the try handler stack for the closest matching enclosing handler
    [[nodiscard]] static std::optional<TryHandler> find_matching_handler(
        std::vector<TryHandler>& handlers,
        size_t current_frames_count
    );
};

} // namespace bvm

#endif // BYTECODE_VM_EXCEPTIONS_HPP
