#include "chunk.hpp"
#include "closure.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace bvm {

namespace {

void write_u32_le(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

void write_u64_le(std::vector<uint8_t>& buf, uint64_t val) {
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
}

uint32_t read_u32_le(const uint8_t* ptr) {
    return static_cast<uint32_t>(ptr[0]) |
          (static_cast<uint32_t>(ptr[1]) << 8) |
          (static_cast<uint32_t>(ptr[2]) << 16) |
          (static_cast<uint32_t>(ptr[3]) << 24);
}

uint64_t read_u64_le(const uint8_t* ptr) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val |= (static_cast<uint64_t>(ptr[i]) << (i * 8));
    }
    return val;
}

} // anonymous namespace

std::vector<uint8_t> Chunk::serialize() const {
    std::vector<uint8_t> payload;

    // 1. Serialize Constant Pool (Feature 3)
    const auto& const_vals = constants.values();
    for (const auto& val : const_vals) {
        payload.push_back(static_cast<uint8_t>(val.type));
        switch (val.type) {
            case ValueType::NIL:
                break;
            case ValueType::BOOL:
                payload.push_back(val.as_bool() ? 1 : 0);
                break;
            case ValueType::INT: {
                uint64_t raw_int;
                int64_t v = val.as_int();
                std::memcpy(&raw_int, &v, sizeof(int64_t));
                write_u64_le(payload, raw_int);
                break;
            }
            case ValueType::FLOAT: {
                uint64_t raw_float;
                double v = val.as_float();
                std::memcpy(&raw_float, &v, sizeof(double));
                write_u64_le(payload, raw_float);
                break;
            }
            case ValueType::STRING: {
                const auto& str = val.as_string();
                write_u32_le(payload, static_cast<uint32_t>(str.size()));
                payload.insert(payload.end(), str.begin(), str.end());
                break;
            }
            case ValueType::FUNCTION: {
                const auto& fn = val.as_function();
                write_u32_le(payload, static_cast<uint32_t>(fn->name.size()));
                payload.insert(payload.end(), fn->name.begin(), fn->name.end());
                write_u32_le(payload, static_cast<uint32_t>(fn->arity));
                auto fn_bytes = fn->chunk.serialize();
                write_u32_le(payload, static_cast<uint32_t>(fn_bytes.size()));
                payload.insert(payload.end(), fn_bytes.begin(), fn_bytes.end());
                break;
            }
            case ValueType::CLOSURE:
            case ValueType::UPVALUE:
            case ValueType::OBJECT:
                break;
        }
    }

    // 2. Append Code Segment
    payload.insert(payload.end(), code.begin(), code.end());

    // 3. Append Debug Line Records (4 bytes per instruction byte)
    for (uint32_t line : lines) {
        write_u32_le(payload, line);
    }

    // 4. Compute CRC32 Checksum over the entire payload (Feature 1)
    uint32_t checksum = compute_crc32(payload.data(), payload.size());

    // 5. Construct 32-Byte Header
    BytecodeHeader header{};
    std::memcpy(header.magic, BVM_MAGIC, 4);
    header.version_major = BVM_VERSION_MAJOR;
    header.version_minor = BVM_VERSION_MINOR;
    header.endianness = BVM_ENDIANNESS_LE;
    std::memset(header.reserved, 0, sizeof(header.reserved));
    header.constant_pool_count = static_cast<uint32_t>(const_vals.size());
    header.code_segment_length = static_cast<uint32_t>(code.size());
    header.entry_point_offset = 0;
    header.checksum_crc32 = checksum;

    // 6. Assemble Final Binary File Buffer
    std::vector<uint8_t> binary(sizeof(BytecodeHeader) + payload.size());
    std::memcpy(binary.data(), &header, sizeof(BytecodeHeader));
    std::memcpy(binary.data() + sizeof(BytecodeHeader), payload.data(), payload.size());

    return binary;
}

Chunk Chunk::deserialize(const uint8_t* data, size_t length) {
    if (length < sizeof(BytecodeHeader)) {
        throw std::runtime_error("Malformed bytecode file: smaller than 32-byte header");
    }

    BytecodeHeader header{};
    std::memcpy(&header, data, sizeof(BytecodeHeader));

    // Validate Magic Identifier "BVM1"
    if (header.magic[0] != BVM_MAGIC[0] ||
        header.magic[1] != BVM_MAGIC[1] ||
        header.magic[2] != BVM_MAGIC[2] ||
        header.magic[3] != BVM_MAGIC[3]) {
        throw std::runtime_error("Invalid bytecode magic identifier (expected 'BVM1')");
    }

    // Validate Version Compatibility
    if (header.version_major != BVM_VERSION_MAJOR) {
        throw std::runtime_error("Incompatible bytecode major version: " +
                                 std::to_string(header.version_major));
    }

    // Verify CRC32 Checksum
    const uint8_t* payload_ptr = data + sizeof(BytecodeHeader);
    size_t payload_len = length - sizeof(BytecodeHeader);
    uint32_t actual_crc = compute_crc32(payload_ptr, payload_len);
    if (actual_crc != header.checksum_crc32) {
        throw std::runtime_error("CRC32 checksum mismatch: corrupted bytecode payload");
    }

    Chunk chunk;
    size_t cursor = 0;

    // 1. Decode Constant Pool (Feature 3)
    for (uint32_t i = 0; i < header.constant_pool_count; ++i) {
        if (cursor >= payload_len) {
            throw std::runtime_error("Unexpected EOF while parsing constant pool");
        }
        uint8_t type_tag = payload_ptr[cursor++];
        switch (static_cast<ValueType>(type_tag)) {
            case ValueType::NIL:
                chunk.constants.add_constant(Value());
                break;
            case ValueType::BOOL: {
                if (cursor >= payload_len) throw std::runtime_error("Truncated boolean constant");
                bool b = (payload_ptr[cursor++] != 0);
                chunk.constants.add_constant(Value(b));
                break;
            }
            case ValueType::INT: {
                if (cursor + 8 > payload_len) throw std::runtime_error("Truncated integer constant");
                uint64_t raw = read_u64_le(payload_ptr + cursor);
                cursor += 8;
                int64_t val;
                std::memcpy(&val, &raw, sizeof(int64_t));
                chunk.constants.add_constant(Value(val));
                break;
            }
            case ValueType::FLOAT: {
                if (cursor + 8 > payload_len) throw std::runtime_error("Truncated float constant");
                uint64_t raw = read_u64_le(payload_ptr + cursor);
                cursor += 8;
                double val;
                std::memcpy(&val, &raw, sizeof(double));
                chunk.constants.add_constant(Value(val));
                break;
            }
            case ValueType::STRING: {
                if (cursor + 4 > payload_len) throw std::runtime_error("Truncated string length");
                uint32_t str_len = read_u32_le(payload_ptr + cursor);
                cursor += 4;
                if (cursor + str_len > payload_len) throw std::runtime_error("Truncated string body");
                std::string s(reinterpret_cast<const char*>(payload_ptr + cursor), str_len);
                cursor += str_len;
                chunk.constants.add_constant(Value(std::move(s)));
                break;
            }
            case ValueType::FUNCTION: {
                if (cursor + 4 > payload_len) throw std::runtime_error("Truncated function name length");
                uint32_t name_len = read_u32_le(payload_ptr + cursor);
                cursor += 4;
                if (cursor + name_len > payload_len) throw std::runtime_error("Truncated function name");
                std::string name(reinterpret_cast<const char*>(payload_ptr + cursor), name_len);
                cursor += name_len;
                if (cursor + 4 > payload_len) throw std::runtime_error("Truncated function arity");
                uint32_t arity = read_u32_le(payload_ptr + cursor);
                cursor += 4;
                if (cursor + 4 > payload_len) throw std::runtime_error("Truncated function chunk length");
                uint32_t fn_size = read_u32_le(payload_ptr + cursor);
                cursor += 4;
                if (cursor + fn_size > payload_len) throw std::runtime_error("Truncated function chunk bytes");
                Chunk fn_chunk = Chunk::deserialize(payload_ptr + cursor, fn_size);
                cursor += fn_size;
                auto fn = std::make_shared<ObjFunction>(std::move(name), arity);
                fn->chunk = std::move(fn_chunk);
                chunk.constants.add_constant(Value(fn));
                break;
            }
            case ValueType::CLOSURE:
            case ValueType::UPVALUE:
                break;
            default:
                throw std::runtime_error("Unknown constant type tag: " + std::to_string(type_tag));
        }
    }

    // 2. Decode Code Segment
    if (cursor + header.code_segment_length > payload_len) {
        throw std::runtime_error("Truncated code segment in bytecode payload");
    }
    chunk.code.assign(payload_ptr + cursor, payload_ptr + cursor + header.code_segment_length);
    cursor += header.code_segment_length;

    // 3. Decode Debug Line Records
    size_t remaining_bytes = payload_len - cursor;
    if (remaining_bytes == header.code_segment_length * 4) {
        chunk.lines.resize(header.code_segment_length);
        for (size_t i = 0; i < header.code_segment_length; ++i) {
            chunk.lines[i] = read_u32_le(payload_ptr + cursor);
            cursor += 4;
        }
    } else {
        chunk.lines.assign(header.code_segment_length, 0);
    }

    return chunk;
}

} // namespace bvm
