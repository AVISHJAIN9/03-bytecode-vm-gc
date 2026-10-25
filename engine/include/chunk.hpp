#ifndef BYTECODE_VM_CHUNK_HPP
#define BYTECODE_VM_CHUNK_HPP

#include "common.hpp"
#include "opcode.hpp"
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>

namespace bvm {

/// Magic identifier for valid Bytecode VM binary files ("BVM1")
inline constexpr uint8_t BVM_MAGIC[4] = { 'B', 'V', 'M', '1' };
inline constexpr uint16_t BVM_VERSION_MAJOR = 1;
inline constexpr uint16_t BVM_VERSION_MINOR = 0;
inline constexpr uint8_t BVM_ENDIANNESS_LE = 1;

/// Packed 32-byte header for binary bytecode containers (Feature 1)
#pragma pack(push, 1)
struct BytecodeHeader {
    uint8_t magic[4];              // 0..4: 'B', 'V', 'M', '1'
    uint16_t version_major;        // 4..6: 1
    uint16_t version_minor;        // 6..8: 0
    uint8_t endianness;            // 8: 1 = Little Endian
    uint8_t reserved[7];           // 9..16: 0-padding
    uint32_t constant_pool_count;  // 16..20: Number of constants in pool
    uint32_t code_segment_length;  // 20..24: Size of code in bytes
    uint32_t entry_point_offset;   // 24..28: Code offset for execution start
    uint32_t checksum_crc32;       // 28..32: CRC32 of payload (constants + code)
};
#pragma pack(pop)

static_assert(sizeof(BytecodeHeader) == 32, "BytecodeHeader must be exactly 32 bytes");

/// Constant Pool manager with deduplication and indexing (Feature 3)
class ConstantPool {
private:
    std::vector<Value> constants_;

public:
    ConstantPool() = default;

    /// Adds a value to the constant pool, deduplicating identical constants.
    /// Returns 16-bit constant index.
    uint16_t add_constant(const Value& val) {
        for (size_t i = 0; i < constants_.size(); ++i) {
            if (constants_[i] == val) {
                return static_cast<uint16_t>(i);
            }
        }
        if (constants_.size() >= 0xFFFF) {
            throw std::overflow_error("Constant pool exceeded 16-bit limit (65535 constants)");
        }
        constants_.push_back(val);
        return static_cast<uint16_t>(constants_.size() - 1);
    }

    [[nodiscard]] const Value& get(uint16_t index) const {
        if (index >= constants_.size()) {
            throw std::out_of_range("Constant pool index out of bounds: " + std::to_string(index));
        }
        return constants_[index];
    }

    [[nodiscard]] size_t size() const noexcept {
        return constants_.size();
    }

    [[nodiscard]] const std::vector<Value>& values() const noexcept {
        return constants_;
    }

    void clear() noexcept {
        constants_.clear();
    }
};

/// A compiled unit of bytecode containing code bytes, source lines, and constants
class Chunk {
public:
    std::vector<uint8_t> code;
    std::vector<uint32_t> lines;
    ConstantPool constants;

    Chunk() = default;

    /// Appends a raw byte with associated source line number
    void write_byte(uint8_t byte, uint32_t line = 0) {
        code.push_back(byte);
        lines.push_back(line);
    }

    /// Appends an opcode
    void write_opcode(Opcode op, uint32_t line = 0) {
        write_byte(static_cast<uint8_t>(op), line);
    }

    /// Appends a 16-bit unsigned integer (little-endian)
    void write_u16(uint16_t val, uint32_t line = 0) {
        write_byte(static_cast<uint8_t>(val & 0xFF), line);
        write_byte(static_cast<uint8_t>((val >> 8) & 0xFF), line);
    }

    /// Appends a 16-bit signed integer (little-endian)
    void write_i16(int16_t val, uint32_t line = 0) {
        write_u16(static_cast<uint16_t>(val), line);
    }

    /// Adds a constant to the pool and emits OP_CONST with 16-bit index
    void emit_constant(const Value& val, uint32_t line = 0) {
        uint16_t idx = constants.add_constant(val);
        write_opcode(Opcode::OP_CONST, line);
        write_u16(idx, line);
    }

    /// Emits a jump instruction with placeholder 16-bit offset. Returns offset to patch.
    size_t emit_jump(Opcode jump_op, uint32_t line = 0) {
        write_opcode(jump_op, line);
        size_t patch_offset = code.size();
        write_i16(0, line); // Placeholder
        return patch_offset;
    }

    /// Patches a jump instruction placeholder to target the current end of code
    void patch_jump(size_t patch_offset) {
        if (patch_offset + 2 > code.size()) {
            throw std::runtime_error("Invalid jump patch offset");
        }
        // Jump offset is relative to the instruction immediately following the jump operand
        int32_t jump_dist = static_cast<int32_t>(code.size()) - static_cast<int32_t>(patch_offset + 2);
        if (jump_dist < -32768 || jump_dist > 32767) {
            throw std::overflow_error("Jump distance exceeded 16-bit range");
        }
        int16_t offset16 = static_cast<int16_t>(jump_dist);
        code[patch_offset] = static_cast<uint8_t>(offset16 & 0xFF);
        code[patch_offset + 1] = static_cast<uint8_t>((offset16 >> 8) & 0xFF);
    }

    /// Serializes chunk to binary container format with header and CRC32 (Feature 1)
    [[nodiscard]] std::vector<uint8_t> serialize() const;

    /// Deserializes chunk from binary container with header and CRC32 verification (Feature 1)
    [[nodiscard]] static Chunk deserialize(const uint8_t* data, size_t length);
    [[nodiscard]] static Chunk deserialize(const std::vector<uint8_t>& bytes) {
        return deserialize(bytes.data(), bytes.size());
    }
};

} // namespace bvm

#endif // BYTECODE_VM_CHUNK_HPP
