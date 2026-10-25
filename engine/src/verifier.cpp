#include "verifier.hpp"
#include "opcode.hpp"
#include "closure.hpp"
#include <queue>
#include <sstream>

namespace bvm {

VerificationResult BytecodeVerifier::verify(const Chunk& chunk) {
    if (chunk.code.empty()) {
        return VerificationResult::failure(
            VerificationError::EMPTY_CODE,
            "Bytecode chunk is empty: no instructions to verify",
            0, 0
        );
    }

    const size_t code_size = chunk.code.size();
    std::vector<bool> is_instruction_boundary(code_size, false);
    size_t last_instruction_offset = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 1: Validate Instruction Boundaries and Operand Encodings
    // ─────────────────────────────────────────────────────────────────────────
    for (size_t offset = 0; offset < code_size;) {
        is_instruction_boundary[offset] = true;
        last_instruction_offset = offset;

        uint8_t byte = chunk.code[offset];
        if (!is_valid_opcode(byte)) {
            std::ostringstream ss;
            ss << "Invalid opcode byte 0x" << std::hex << static_cast<int>(byte)
               << " at offset " << std::dec << offset;
            return VerificationResult::failure(
                VerificationError::INVALID_OPCODE,
                ss.str(),
                offset, 0
            );
        }

        auto op = static_cast<Opcode>(byte);
        const auto& info = get_opcode_info(op);

        size_t full_instruction_size = 1 + info.operand_size;
        if (op == Opcode::OP_CLOSURE) {
            if (offset + 3 > code_size) {
                return VerificationResult::failure(
                    VerificationError::TRUNCATED_OPERAND,
                    "Truncated OP_CLOSURE header",
                    offset, 0
                );
            }
            uint16_t const_idx = static_cast<uint16_t>(chunk.code[offset + 1]) |
                                (static_cast<uint16_t>(chunk.code[offset + 2]) << 8);
            if (const_idx >= chunk.constants.size() || !chunk.constants.get(const_idx).is_function()) {
                return VerificationResult::failure(
                    VerificationError::CONSTANT_INDEX_OUT_OF_BOUNDS,
                    "OP_CLOSURE target constant is not a valid ObjFunction",
                    offset, 0
                );
            }
            auto fn = chunk.constants.get(const_idx).as_function();
            full_instruction_size = 3 + (fn->upvalue_descs.size() * 3);
        }

        if (offset + full_instruction_size > code_size) {
            std::ostringstream ss;
            ss << "Truncated operand for " << info.name << " at offset " << offset
               << " (expected " << full_instruction_size << " bytes, got "
               << (code_size - offset) << ")";
            return VerificationResult::failure(
                VerificationError::TRUNCATED_OPERAND,
                ss.str(),
                offset, 0
            );
        }

        // Validate constant pool reference
        if (op == Opcode::OP_CONST || op == Opcode::OP_GET_GLOBAL || op == Opcode::OP_SET_GLOBAL ||
            op == Opcode::OP_GET_PROPERTY || op == Opcode::OP_SET_PROPERTY) {
            uint16_t const_idx = static_cast<uint16_t>(chunk.code[offset + 1]) |
                                (static_cast<uint16_t>(chunk.code[offset + 2]) << 8);
            if (const_idx >= chunk.constants.size()) {
                std::ostringstream ss;
                ss << "Constant pool index " << const_idx << " out of bounds (pool size: "
                   << chunk.constants.size() << ") at offset " << offset;
                return VerificationResult::failure(
                    VerificationError::CONSTANT_INDEX_OUT_OF_BOUNDS,
                    ss.str(),
                    offset, 0
                );
            }
        }

        offset += full_instruction_size;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 2: Validate Terminal Instruction (Deterministic Termination)
    // ─────────────────────────────────────────────────────────────────────────
    auto last_op = static_cast<Opcode>(chunk.code[last_instruction_offset]);
    if (last_op != Opcode::OP_HALT &&
        last_op != Opcode::OP_JUMP &&
        last_op != Opcode::OP_RETURN &&
        last_op != Opcode::OP_THROW) {
        return VerificationResult::failure(
            VerificationError::MISSING_TERMINAL_HALT,
            "Bytecode does not terminate with OP_HALT, OP_RETURN, OP_THROW, or an unconditional exit",
            last_instruction_offset, 0
        );
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 3: Validate Jump Target Alignment and Bounds
    // ─────────────────────────────────────────────────────────────────────────
    for (size_t offset = 0; offset < code_size;) {
        auto op = static_cast<Opcode>(chunk.code[offset]);
        const auto& info = get_opcode_info(op);
        size_t full_size = 1 + info.operand_size;

        if (op == Opcode::OP_CLOSURE) {
            uint16_t const_idx = static_cast<uint16_t>(chunk.code[offset + 1]) |
                                (static_cast<uint16_t>(chunk.code[offset + 2]) << 8);
            auto fn = chunk.constants.get(const_idx).as_function();
            full_size = 3 + (fn->upvalue_descs.size() * 3);
        } else if (info.is_branch || op == Opcode::OP_PUSH_TRY) {
            uint16_t raw_jump = static_cast<uint16_t>(chunk.code[offset + 1]) |
                               (static_cast<uint16_t>(chunk.code[offset + 2]) << 8);
            int16_t jump_offset = static_cast<int16_t>(raw_jump);
            int64_t target = static_cast<int64_t>(offset) + 3 + jump_offset;

            if (target < 0 || static_cast<size_t>(target) >= code_size) {
                std::ostringstream ss;
                ss << "Jump target out of bounds at offset " << offset
                   << ": destination offset " << target << " outside code segment [0, "
                   << code_size << ")";
                return VerificationResult::failure(
                    VerificationError::JUMP_OUT_OF_BOUNDS,
                    ss.str(),
                    offset, 0
                );
            }

            if (!is_instruction_boundary[static_cast<size_t>(target)]) {
                std::ostringstream ss;
                ss << "Illegal jump into middle of instruction at offset " << offset
                   << " -> target " << target;
                return VerificationResult::failure(
                    VerificationError::JUMP_INTO_INSTRUCTION_MIDDLE,
                    ss.str(),
                    offset, 0
                );
            }
        }

        offset += full_size;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 4: Abstract Interpretation CFG Stack Height Simulation
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<int32_t> stack_depth_at(code_size, -1);
    std::queue<std::pair<size_t, int32_t>> worklist;

    // Entry point: offset 0 with stack depth 0
    worklist.push({0, 0});

    while (!worklist.empty()) {
        auto [offset, current_depth] = worklist.front();
        worklist.pop();

        if (stack_depth_at[offset] != -1) {
            // Merge point check: both paths reaching this instruction must have identical stack depth
            if (stack_depth_at[offset] != current_depth) {
                std::ostringstream ss;
                ss << "Stack height mismatch at merge point offset " << offset
                   << ": conflicting heights " << stack_depth_at[offset] << " vs " << current_depth;
                return VerificationResult::failure(
                    VerificationError::STACK_HEIGHT_MISMATCH_AT_MERGE,
                    ss.str(),
                    offset, current_depth
                );
            }
            continue; // Already verified this state
        }

        stack_depth_at[offset] = current_depth;

        auto op = static_cast<Opcode>(chunk.code[offset]);
        const auto& info = get_opcode_info(op);

        size_t full_size = 1 + info.operand_size;
        int8_t effect = info.stack_effect;
        uint8_t min_req = info.min_stack_req;

        if (op == Opcode::OP_CLOSURE) {
            uint16_t const_idx = static_cast<uint16_t>(chunk.code[offset + 1]) |
                                (static_cast<uint16_t>(chunk.code[offset + 2]) << 8);
            auto fn = chunk.constants.get(const_idx).as_function();
            full_size = 3 + (fn->upvalue_descs.size() * 3);
        } else if (op == Opcode::OP_CALL) {
            uint8_t argc = chunk.code[offset + 1];
            min_req = argc + 1; // Callee + args
            effect = -static_cast<int8_t>(argc); // Consumes args, callee replaced with result on return
        } else if (op == Opcode::OP_CALL_NATIVE) {
            uint8_t argc = chunk.code[offset + 3];
            min_req = argc;
            effect = 1 - static_cast<int8_t>(argc);
        }

        // Check for stack underflow before executing instruction
        if (current_depth < min_req) {
            std::ostringstream ss;
            ss << "Stack underflow at " << info.name << " (offset " << offset
               << "): required " << static_cast<int>(min_req)
               << " values on stack, but only " << current_depth << " available";
            return VerificationResult::failure(
                VerificationError::STACK_UNDERFLOW,
                ss.str(),
                offset, current_depth
            );
        }

        int32_t next_depth = current_depth + effect;
        if (next_depth > MAX_STACK_DEPTH) {
            return VerificationResult::failure(
                VerificationError::STACK_OVERFLOW,
                "Maximum static stack depth exceeded (1024 slots) at offset " + std::to_string(offset),
                offset, next_depth
            );
        }

        if (op == Opcode::OP_HALT || op == Opcode::OP_RETURN || op == Opcode::OP_THROW) {
            // Execution path terminates
            continue;
        }

        if (op == Opcode::OP_JUMP) {
            uint16_t raw_jump = static_cast<uint16_t>(chunk.code[offset + 1]) |
                               (static_cast<uint16_t>(chunk.code[offset + 2]) << 8);
            int16_t jump_offset = static_cast<int16_t>(raw_jump);
            size_t target = static_cast<size_t>(static_cast<int64_t>(offset) + 3 + jump_offset);
            worklist.push({target, next_depth});
            continue;
        }

        if (op == Opcode::OP_JUMP_IF_TRUE || op == Opcode::OP_JUMP_IF_FALSE) {
            uint16_t raw_jump = static_cast<uint16_t>(chunk.code[offset + 1]) |
                               (static_cast<uint16_t>(chunk.code[offset + 2]) << 8);
            int16_t jump_offset = static_cast<int16_t>(raw_jump);
            size_t target = static_cast<size_t>(static_cast<int64_t>(offset) + 3 + jump_offset);

            // Conditional branch forks into two paths: jump target and fallthrough
            worklist.push({target, next_depth});
            size_t fallthrough = offset + 3;
            if (fallthrough < code_size) {
                worklist.push({fallthrough, next_depth});
            }
            continue;
        }

        if (op == Opcode::OP_PUSH_TRY) {
            uint16_t raw_jump = static_cast<uint16_t>(chunk.code[offset + 1]) |
                               (static_cast<uint16_t>(chunk.code[offset + 2]) << 8);
            int16_t jump_offset = static_cast<int16_t>(raw_jump);
            size_t catch_target = static_cast<size_t>(static_cast<int64_t>(offset) + 3 + jump_offset);

            // Catch handler receives the thrown exception (+1 value on stack)
            worklist.push({catch_target, next_depth + 1});
            size_t fallthrough = offset + 3;
            if (fallthrough < code_size) {
                worklist.push({fallthrough, next_depth});
            }
            continue;
        }

        // Standard sequential instruction: proceed to fallthrough
        size_t next_offset = offset + full_size;
        if (next_offset < code_size) {
            worklist.push({next_offset, next_depth});
        }
    }

    return VerificationResult::success();
}

} // namespace bvm
