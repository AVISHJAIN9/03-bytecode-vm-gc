#ifndef BYTECODE_VM_VALUE_HPP
#define BYTECODE_VM_VALUE_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>
#include <memory>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <iomanip>

namespace bvm {

// Forward declarations of managed object types
class ObjFunction;
class ObjClosure;
class ObjUpvalue;
struct ObjHeader;

/// Supported Value Types in the Virtual Machine
enum class ValueType : uint8_t {
    NIL = 0,
    BOOL = 1,
    INT = 2,
    FLOAT = 3,
    STRING = 4,
    FUNCTION = 5,
    CLOSURE = 6,
    UPVALUE = 7,
    OBJECT = 8
};

/// Tagged Value representation supporting primitives, closures, and GC heap objects
struct Value {
    ValueType type;
    std::variant<
        std::monostate,
        bool,
        int64_t,
        double,
        std::string,
        std::shared_ptr<ObjFunction>,
        std::shared_ptr<ObjClosure>,
        std::shared_ptr<ObjUpvalue>,
        ObjHeader*
    > as;

    Value() : type(ValueType::NIL), as(std::monostate{}) {}
    explicit Value(bool b) : type(ValueType::BOOL), as(b) {}
    explicit Value(int64_t i) : type(ValueType::INT), as(i) {}
    explicit Value(int i) : type(ValueType::INT), as(static_cast<int64_t>(i)) {}
    explicit Value(double d) : type(ValueType::FLOAT), as(d) {}
    explicit Value(std::string s) : type(ValueType::STRING), as(std::move(s)) {}
    explicit Value(const char* s) : type(ValueType::STRING), as(std::string(s)) {}
    explicit Value(std::shared_ptr<ObjFunction> fn);
    explicit Value(std::shared_ptr<ObjClosure> cl);
    explicit Value(std::shared_ptr<ObjUpvalue> up);
    explicit Value(ObjHeader* obj) : type(ValueType::OBJECT), as(obj) {}

    [[nodiscard]] bool is_nil() const noexcept { return type == ValueType::NIL; }
    [[nodiscard]] bool is_bool() const noexcept { return type == ValueType::BOOL; }
    [[nodiscard]] bool is_int() const noexcept { return type == ValueType::INT; }
    [[nodiscard]] bool is_float() const noexcept { return type == ValueType::FLOAT; }
    [[nodiscard]] bool is_string() const noexcept { return type == ValueType::STRING; }
    [[nodiscard]] bool is_number() const noexcept { return is_int() || is_float(); }
    [[nodiscard]] bool is_function() const noexcept { return type == ValueType::FUNCTION; }
    [[nodiscard]] bool is_closure() const noexcept { return type == ValueType::CLOSURE; }
    [[nodiscard]] bool is_upvalue() const noexcept { return type == ValueType::UPVALUE; }
    [[nodiscard]] bool is_object() const noexcept { return type == ValueType::OBJECT; }

    [[nodiscard]] bool as_bool() const {
        if (!is_bool()) throw std::runtime_error("Type error: expected boolean");
        return std::get<bool>(as);
    }

    [[nodiscard]] int64_t as_int() const {
        if (!is_int()) throw std::runtime_error("Type error: expected integer");
        return std::get<int64_t>(as);
    }

    [[nodiscard]] double as_float() const {
        if (!is_float()) throw std::runtime_error("Type error: expected float");
        return std::get<double>(as);
    }

    [[nodiscard]] double as_number() const {
        if (is_int()) return static_cast<double>(std::get<int64_t>(as));
        if (is_float()) return std::get<double>(as);
        throw std::runtime_error("Type error: expected number (int or float)");
    }

    [[nodiscard]] const std::string& as_string() const {
        if (!is_string()) throw std::runtime_error("Type error: expected string");
        return std::get<std::string>(as);
    }

    [[nodiscard]] std::shared_ptr<ObjFunction> as_function() const;
    [[nodiscard]] std::shared_ptr<ObjClosure> as_closure() const;
    [[nodiscard]] std::shared_ptr<ObjUpvalue> as_upvalue() const;

    [[nodiscard]] ObjHeader* as_object() const {
        if (!is_object()) throw std::runtime_error("Type error: expected heap object");
        return std::get<ObjHeader*>(as);
    }

    [[nodiscard]] bool is_truthy() const noexcept {
        if (is_nil()) return false;
        if (is_bool()) return std::get<bool>(as);
        if (is_int()) return std::get<int64_t>(as) != 0;
        if (is_float()) return std::get<double>(as) != 0.0;
        if (is_string()) return !std::get<std::string>(as).empty();
        if (is_object()) return std::get<ObjHeader*>(as) != nullptr;
        return true;
    }

    [[nodiscard]] std::string to_string() const;

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }
};

/// IEEE 802.3 CRC32 calculation table for bytecode container verification
inline uint32_t compute_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

} // namespace bvm

#endif // BYTECODE_VM_VALUE_HPP
