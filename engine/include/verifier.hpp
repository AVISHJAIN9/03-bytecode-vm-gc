#ifndef BYTECODE_VM_VERIFIER_HPP
#define BYTECODE_VM_VERIFIER_HPP

#include "chunk.hpp"
#include <string>

namespace bvm {

/// Enumeration of bytecode verification errors (Feature 5)
enum class VerificationError : uint8_t {
    OK = 0,
    EMPTY_CODE,
    INVALID_OPCODE,
    TRUNCATED_OPERAND,
    CONSTANT_INDEX_OUT_OF_BOUNDS,
    STACK_UNDERFLOW,
    STACK_OVERFLOW,
    STACK_HEIGHT_MISMATCH_AT_MERGE,
    JUMP_OUT_OF_BOUNDS,
    JUMP_INTO_INSTRUCTION_MIDDLE,
    MISSING_TERMINAL_HALT
};

/// Result of static bytecode verification
struct VerificationResult {
    bool is_valid;
    VerificationError error;
    std::string message;
    size_t error_offset;
    int32_t stack_depth;

    [[nodiscard]] static VerificationResult success() {
        return { true, VerificationError::OK, "Verification succeeded: bytecode is safe", 0, 0 };
    }

    [[nodiscard]] static VerificationResult failure(
        VerificationError err,
        const std::string& msg,
        size_t offset = 0,
        int32_t depth = 0
    ) {
        return { false, err, msg, offset, depth };
    }
};

/// Static Bytecode Verifier (Feature 5)
class BytecodeVerifier {
public:
    static constexpr int32_t MAX_STACK_DEPTH = 1024;

    /// Statically verifies chunk invariants prior to VM execution.
    /// Rejects malformed opcodes, out-of-bounds jumps, stack underflows, and inconsistent merges.
    [[nodiscard]] static VerificationResult verify(const Chunk& chunk);
};

} // namespace bvm

#endif // BYTECODE_VM_VERIFIER_HPP
