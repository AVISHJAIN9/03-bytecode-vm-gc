#ifndef BYTECODE_VM_OPCODE_HPP
#define BYTECODE_VM_OPCODE_HPP

#include <cstdint>
#include <string_view>
#include <array>

namespace bvm {

/// Enumeration of Bytecode Virtual Machine Opcodes (Features 2, 7, 8, 9, 13)
enum class Opcode : uint8_t {
    // Constants
    OP_NOP          = 0x00,
    OP_CONST        = 0x01, // 2-byte operand: uint16_t constant pool index
    OP_NIL          = 0x02,
    OP_TRUE         = 0x03,
    OP_FALSE        = 0x04,

    // Stack Manipulation
    OP_POP          = 0x05,
    OP_DUP          = 0x06,
    OP_SWAP         = 0x07,

    // Arithmetic & Bitwise
    OP_ADD          = 0x08,
    OP_SUB          = 0x09,
    OP_MUL          = 0x0A,
    OP_DIV          = 0x0B,
    OP_MOD          = 0x0C,
    OP_NEG          = 0x0D,
    OP_BIT_AND      = 0x0E,
    OP_BIT_OR       = 0x0F,
    OP_BIT_XOR      = 0x10,
    OP_BIT_NOT      = 0x11,

    // Comparison & Logic
    OP_EQ           = 0x12,
    OP_NEQ          = 0x13,
    OP_LT           = 0x14,
    OP_LTE          = 0x15,
    OP_GT           = 0x16,
    OP_GTE          = 0x17,
    OP_NOT          = 0x18,

    // Control Flow
    OP_JUMP         = 0x19, // 2-byte operand: int16_t relative offset
    OP_JUMP_IF_TRUE = 0x1A, // 2-byte operand: int16_t relative offset
    OP_JUMP_IF_FALSE= 0x1B, // 2-byte operand: int16_t relative offset
    OP_HALT         = 0x1C,

    // Local Variables & Call Frames (Feature 7)
    OP_GET_LOCAL    = 0x1D, // 2-byte operand: uint16_t slot index
    OP_SET_LOCAL    = 0x1E, // 2-byte operand: uint16_t slot index
    OP_CALL         = 0x1F, // 1-byte operand: uint8_t argument count
    OP_RETURN       = 0x20, // 0-byte operand: returns top value to caller

    // Closures & Upvalues (Feature 8)
    OP_CLOSURE      = 0x21, // 2-byte operand: uint16_t function constant index + upvalue descriptors
    OP_GET_UPVALUE  = 0x22, // 2-byte operand: uint16_t upvalue index
    OP_SET_UPVALUE  = 0x23, // 2-byte operand: uint16_t upvalue index
    OP_CLOSE_UPVALUE= 0x24, // 0-byte operand: pops top of stack and closes upvalue

    // Structured Exception Handling (Feature 9)
    OP_PUSH_TRY     = 0x25, // 2-byte operand: int16_t relative offset to catch block
    OP_POP_TRY      = 0x26, // 0-byte operand: pops active try handler
    OP_THROW        = 0x27, // 0-byte operand: throws top of stack value

    // Global Variables & Native/FFI Interface (Feature 13)
    OP_GET_GLOBAL   = 0x28, // 2-byte operand: uint16_t constant pool string index
    OP_SET_GLOBAL   = 0x29, // 2-byte operand: uint16_t constant pool string index
    OP_CALL_NATIVE  = 0x2A, // 3-byte operand: uint16_t native function index + uint8_t argc

    // Heap Object Property & Element Access with Write Barrier (Feature 22)
    OP_GET_PROPERTY = 0x2B, // 2-byte operand: uint16_t constant pool string index
    OP_SET_PROPERTY = 0x2C, // 2-byte operand: uint16_t constant pool string index
    OP_GET_ELEMENT  = 0x2D, // 0-byte operand: index + array on stack
    OP_SET_ELEMENT  = 0x2E, // 0-byte operand: value + index + array on stack

    // Boundary marker for total opcode count
    OP_COUNT        = 0x2F
};

/// Metadata description for each opcode
struct OpcodeInfo {
    Opcode opcode;
    std::string_view name;
    uint8_t operand_size;    // Bytes of operands following the opcode
    int8_t stack_effect;     // Net change in stack depth (+1, -1, 0)
    uint8_t min_stack_req;   // Minimum values required on stack before execution
    bool is_branch;          // Whether opcode can alter control flow
    bool is_unconditional;   // Whether branch is unconditional (like JUMP, HALT, RETURN, THROW)
};

inline constexpr std::array<OpcodeInfo, static_cast<size_t>(Opcode::OP_COUNT)> OPCODE_TABLE = {{
    // Opcode,                 Name,               OpSize, StackEffect, MinStackReq, IsBranch, IsUncond
    { Opcode::OP_NOP,          "OP_NOP",           0,  0, 0, false, false },
    { Opcode::OP_CONST,        "OP_CONST",         2, +1, 0, false, false },
    { Opcode::OP_NIL,          "OP_NIL",           0, +1, 0, false, false },
    { Opcode::OP_TRUE,         "OP_TRUE",          0, +1, 0, false, false },
    { Opcode::OP_FALSE,        "OP_FALSE",         0, +1, 0, false, false },

    { Opcode::OP_POP,          "OP_POP",           0, -1, 1, false, false },
    { Opcode::OP_DUP,          "OP_DUP",           0, +1, 1, false, false },
    { Opcode::OP_SWAP,         "OP_SWAP",          0,  0, 2, false, false },

    { Opcode::OP_ADD,          "OP_ADD",           0, -1, 2, false, false },
    { Opcode::OP_SUB,          "OP_SUB",           0, -1, 2, false, false },
    { Opcode::OP_MUL,          "OP_MUL",           0, -1, 2, false, false },
    { Opcode::OP_DIV,          "OP_DIV",           0, -1, 2, false, false },
    { Opcode::OP_MOD,          "OP_MOD",           0, -1, 2, false, false },
    { Opcode::OP_NEG,          "OP_NEG",           0,  0, 1, false, false },
    { Opcode::OP_BIT_AND,      "OP_BIT_AND",       0, -1, 2, false, false },
    { Opcode::OP_BIT_OR,       "OP_BIT_OR",        0, -1, 2, false, false },
    { Opcode::OP_BIT_XOR,      "OP_BIT_XOR",       0, -1, 2, false, false },
    { Opcode::OP_BIT_NOT,      "OP_BIT_NOT",       0,  0, 1, false, false },

    { Opcode::OP_EQ,           "OP_EQ",            0, -1, 2, false, false },
    { Opcode::OP_NEQ,          "OP_NEQ",           0, -1, 2, false, false },
    { Opcode::OP_LT,           "OP_LT",            0, -1, 2, false, false },
    { Opcode::OP_LTE,          "OP_LTE",           0, -1, 2, false, false },
    { Opcode::OP_GT,           "OP_GT",            0, -1, 2, false, false },
    { Opcode::OP_GTE,          "OP_GTE",           0, -1, 2, false, false },
    { Opcode::OP_NOT,          "OP_NOT",           0,  0, 1, false, false },

    { Opcode::OP_JUMP,         "OP_JUMP",          2,  0, 0, true,  true  },
    { Opcode::OP_JUMP_IF_TRUE, "OP_JUMP_IF_TRUE",  2, -1, 1, true,  false },
    { Opcode::OP_JUMP_IF_FALSE,"OP_JUMP_IF_FALSE", 2, -1, 1, true,  false },
    { Opcode::OP_HALT,         "OP_HALT",          0,  0, 0, false, true  },

    { Opcode::OP_GET_LOCAL,    "OP_GET_LOCAL",     2, +1, 0, false, false },
    { Opcode::OP_SET_LOCAL,    "OP_SET_LOCAL",     2,  0, 1, false, false },
    { Opcode::OP_CALL,         "OP_CALL",          1,  0, 1, false, false },
    { Opcode::OP_RETURN,       "OP_RETURN",        0, -1, 1, false, true  },

    { Opcode::OP_CLOSURE,      "OP_CLOSURE",       2, +1, 0, false, false },
    { Opcode::OP_GET_UPVALUE,  "OP_GET_UPVALUE",   2, +1, 0, false, false },
    { Opcode::OP_SET_UPVALUE,  "OP_SET_UPVALUE",   2,  0, 1, false, false },
    { Opcode::OP_CLOSE_UPVALUE,"OP_CLOSE_UPVALUE", 0, -1, 1, false, false },

    { Opcode::OP_PUSH_TRY,     "OP_PUSH_TRY",      2,  0, 0, false, false },
    { Opcode::OP_POP_TRY,      "OP_POP_TRY",       0,  0, 0, false, false },
    { Opcode::OP_THROW,        "OP_THROW",         0, -1, 1, false, true  },

    { Opcode::OP_GET_GLOBAL,   "OP_GET_GLOBAL",    2, +1, 0, false, false },
    { Opcode::OP_SET_GLOBAL,   "OP_SET_GLOBAL",    2,  0, 1, false, false },
    { Opcode::OP_CALL_NATIVE,  "OP_CALL_NATIVE",   3,  0, 0, false, false },

    { Opcode::OP_GET_PROPERTY, "OP_GET_PROPERTY",  2,  0, 1, false, false },
    { Opcode::OP_SET_PROPERTY, "OP_SET_PROPERTY",  2, -1, 2, false, false },
    { Opcode::OP_GET_ELEMENT,  "OP_GET_ELEMENT",   0, -1, 2, false, false },
    { Opcode::OP_SET_ELEMENT,  "OP_SET_ELEMENT",   0, -2, 3, false, false },
}};

[[nodiscard]] inline constexpr bool is_valid_opcode(uint8_t byte) noexcept {
    return byte < static_cast<uint8_t>(Opcode::OP_COUNT);
}

[[nodiscard]] inline constexpr const OpcodeInfo& get_opcode_info(Opcode op) noexcept {
    return OPCODE_TABLE[static_cast<size_t>(op)];
}

} // namespace bvm

#endif // BYTECODE_VM_OPCODE_HPP
