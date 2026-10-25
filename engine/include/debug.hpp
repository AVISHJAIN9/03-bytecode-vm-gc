#ifndef BYTECODE_VM_DEBUG_HPP
#define BYTECODE_VM_DEBUG_HPP

#include "call_frame.hpp"
#include <string>
#include <vector>

namespace bvm {

/// Detailed information about a single stack frame at the point of backtrace
struct StackFrameInfo {
    std::string function_name;
    size_t ip;
    uint32_t line;
    size_t frame_index;
    size_t slot_offset;
};

/// Backtrace generator providing deterministic call stack backtraces (Feature 10)
class Backtrace {
public:
    /// Captures stack frame details from the active call stack
    [[nodiscard]] static std::vector<StackFrameInfo> capture(const std::vector<CallFrame>& frames);

    /// Formats a complete, human-readable backtrace with function names, line numbers, and offsets
    [[nodiscard]] static std::string format(
        const std::vector<CallFrame>& frames,
        const std::string& error_header = ""
    );
};

} // namespace bvm

#endif // BYTECODE_VM_DEBUG_HPP
