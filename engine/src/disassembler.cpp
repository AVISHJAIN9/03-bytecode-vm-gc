#include "disassembler.hpp"
#include "opcode.hpp"
#include "closure.hpp"
#include <iomanip>

namespace bvm {

namespace {

size_t simple_instruction(const char* name, size_t offset, std::ostream& out) {
    out << name << "\n";
    return offset + 1;
}

size_t byte_instruction(const char* name, const Chunk& chunk, size_t offset, std::ostream& out) {
    if (offset + 1 >= chunk.code.size()) {
        out << name << " <truncated operand>\n";
        return chunk.code.size();
    }
    uint8_t byte = chunk.code[offset + 1];
    out << std::left << std::setw(16) << name << " "
        << std::right << std::setw(5) << static_cast<int>(byte) << "\n";
    return offset + 2;
}

size_t u16_instruction(const char* name, const Chunk& chunk, size_t offset, std::ostream& out) {
    if (offset + 2 >= chunk.code.size()) {
        out << name << " <truncated operand>\n";
        return chunk.code.size();
    }
    uint16_t index = static_cast<uint16_t>(chunk.code[offset + 1]) |
                    (static_cast<uint16_t>(chunk.code[offset + 2]) << 8);
    out << std::left << std::setw(16) << name << " "
        << std::right << std::setw(5) << index << "\n";
    return offset + 3;
}

size_t constant_instruction(const char* name, const Chunk& chunk, size_t offset, std::ostream& out) {
    if (offset + 2 >= chunk.code.size()) {
        out << name << " <truncated operand>\n";
        return chunk.code.size();
    }
    uint16_t const_idx = static_cast<uint16_t>(chunk.code[offset + 1]) |
                        (static_cast<uint16_t>(chunk.code[offset + 2]) << 8);

    out << std::left << std::setw(16) << name << " "
        << std::right << std::setw(5) << const_idx << " '";
    if (const_idx < chunk.constants.size()) {
        out << chunk.constants.get(const_idx).to_string();
    } else {
        out << "<out of bounds>";
    }
    out << "'\n";
    return offset + 3;
}

size_t jump_instruction(const char* name, int sign, const Chunk& chunk, size_t offset, std::ostream& out) {
    if (offset + 2 >= chunk.code.size()) {
        out << name << " <truncated operand>\n";
        return chunk.code.size();
    }
    uint16_t raw_jump = static_cast<uint16_t>(chunk.code[offset + 1]) |
                       (static_cast<uint16_t>(chunk.code[offset + 2]) << 8);
    int16_t jump_offset = static_cast<int16_t>(raw_jump);
    size_t target = offset + 3 + sign * jump_offset;

    out << std::left << std::setw(16) << name << " "
        << (jump_offset >= 0 ? "+" : "") << jump_offset
        << " -> " << std::right << std::setw(4) << std::setfill('0') << target << std::setfill(' ') << "\n";

    return offset + 3;
}

} // anonymous namespace

void disassemble_chunk(const Chunk& chunk, const std::string& name, std::ostream& out) {
    out << "== " << name << " (Code size: " << chunk.code.size()
        << " bytes, Constants: " << chunk.constants.size() << ") ==\n";

    for (size_t offset = 0; offset < chunk.code.size();) {
        offset = disassemble_instruction(chunk, offset, out);
    }
    out << "========================================\n";
}

size_t disassemble_instruction(const Chunk& chunk, size_t offset, std::ostream& out) {
    out << std::right << std::setw(4) << std::setfill('0') << offset << " " << std::setfill(' ');

    if (offset > 0 && offset < chunk.lines.size() && chunk.lines[offset] == chunk.lines[offset - 1]) {
        out << "   | ";
    } else if (offset < chunk.lines.size()) {
        out << std::setw(4) << chunk.lines[offset] << " ";
    } else {
        out << "   ? ";
    }

    uint8_t instruction = chunk.code[offset];
    if (!is_valid_opcode(instruction)) {
        out << "Unknown opcode 0x" << std::hex << static_cast<int>(instruction) << std::dec << "\n";
        return offset + 1;
    }

    auto op = static_cast<Opcode>(instruction);
    const auto& info = get_opcode_info(op);

    switch (op) {
        case Opcode::OP_NOP:
        case Opcode::OP_NIL:
        case Opcode::OP_TRUE:
        case Opcode::OP_FALSE:
        case Opcode::OP_POP:
        case Opcode::OP_DUP:
        case Opcode::OP_SWAP:
        case Opcode::OP_ADD:
        case Opcode::OP_SUB:
        case Opcode::OP_MUL:
        case Opcode::OP_DIV:
        case Opcode::OP_MOD:
        case Opcode::OP_NEG:
        case Opcode::OP_BIT_AND:
        case Opcode::OP_BIT_OR:
        case Opcode::OP_BIT_XOR:
        case Opcode::OP_BIT_NOT:
        case Opcode::OP_EQ:
        case Opcode::OP_NEQ:
        case Opcode::OP_LT:
        case Opcode::OP_LTE:
        case Opcode::OP_GT:
        case Opcode::OP_GTE:
        case Opcode::OP_NOT:
        case Opcode::OP_HALT:
        case Opcode::OP_RETURN:
        case Opcode::OP_CLOSE_UPVALUE:
        case Opcode::OP_POP_TRY:
        case Opcode::OP_THROW:
        case Opcode::OP_GET_ELEMENT:
        case Opcode::OP_SET_ELEMENT:
            return simple_instruction(info.name.data(), offset, out);

        case Opcode::OP_CALL:
            return byte_instruction(info.name.data(), chunk, offset, out);

        case Opcode::OP_GET_LOCAL:
        case Opcode::OP_SET_LOCAL:
        case Opcode::OP_GET_UPVALUE:
        case Opcode::OP_SET_UPVALUE:
            return u16_instruction(info.name.data(), chunk, offset, out);

        case Opcode::OP_CONST:
        case Opcode::OP_GET_GLOBAL:
        case Opcode::OP_SET_GLOBAL:
        case Opcode::OP_GET_PROPERTY:
        case Opcode::OP_SET_PROPERTY:
            return constant_instruction(info.name.data(), chunk, offset, out);

        case Opcode::OP_CALL_NATIVE: {
            if (offset + 3 >= chunk.code.size()) {
                out << info.name << " <truncated operand>\n";
                return chunk.code.size();
            }
            uint16_t fn_idx = static_cast<uint16_t>(chunk.code[offset + 1]) |
                             (static_cast<uint16_t>(chunk.code[offset + 2]) << 8);
            uint8_t argc = chunk.code[offset + 3];
            out << std::left << std::setw(16) << info.name << " "
                << "fn:" << fn_idx << " argc:" << static_cast<int>(argc) << "\n";
            return offset + 4;
        }

        case Opcode::OP_CLOSURE: {
            size_t next_off = constant_instruction(info.name.data(), chunk, offset, out);
            uint16_t const_idx = static_cast<uint16_t>(chunk.code[offset + 1]) |
                                (static_cast<uint16_t>(chunk.code[offset + 2]) << 8);
            if (const_idx < chunk.constants.size() && chunk.constants.get(const_idx).is_function()) {
                auto fn = chunk.constants.get(const_idx).as_function();
                for (size_t j = 0; j < fn->upvalue_descs.size(); ++j) {
                    if (next_off + 2 >= chunk.code.size()) break;
                    uint8_t is_local = chunk.code[next_off++];
                    uint16_t idx = static_cast<uint16_t>(chunk.code[next_off]) |
                                  (static_cast<uint16_t>(chunk.code[next_off + 1]) << 8);
                    next_off += 2;
                    out << "      |                     "
                        << (is_local ? "local " : "upvalue ") << idx << "\n";
                }
            }
            return next_off;
        }

        case Opcode::OP_JUMP:
        case Opcode::OP_JUMP_IF_TRUE:
        case Opcode::OP_JUMP_IF_FALSE:
        case Opcode::OP_PUSH_TRY:
            return jump_instruction(info.name.data(), 1, chunk, offset, out);

        default:
            out << "Unimplemented disassembly for opcode " << static_cast<int>(instruction) << "\n";
            return offset + 1;
    }
}

} // namespace bvm
