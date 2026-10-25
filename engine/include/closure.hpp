#ifndef BYTECODE_VM_CLOSURE_HPP
#define BYTECODE_VM_CLOSURE_HPP

#include "chunk.hpp"
#include "value.hpp"
#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace bvm {

/// Description of an upvalue captured by a function prototype
struct UpvalueDesc {
    bool is_local;       // true if capturing directly from immediate enclosing local stack; false if capturing enclosing closure's upvalue
    uint16_t index;     // slot index or enclosing upvalue index
};

/// First-class compiled function object (Feature 7)
class ObjFunction {
public:
    std::string name;
    size_t arity{0};
    std::vector<UpvalueDesc> upvalue_descs;
    Chunk chunk;

    explicit ObjFunction(std::string name = "<script>", size_t arity = 0)
        : name(std::move(name)), arity(arity) {}

    /// Registers an upvalue requirement on this function
    size_t add_upvalue(bool is_local, uint16_t index) {
        // Check if an existing upvalue description matches
        for (size_t i = 0; i < upvalue_descs.size(); ++i) {
            if (upvalue_descs[i].is_local == is_local && upvalue_descs[i].index == index) {
                return i;
            }
        }
        upvalue_descs.push_back(UpvalueDesc{is_local, index});
        return upvalue_descs.size() - 1;
    }
};

/// Lexical Upvalue reference (Feature 8)
/// Points directly to a stack slot while open, migrates to heap-allocated closed field when frame exits
class ObjUpvalue {
public:
    Value* location{nullptr};                  // Pointer to active value (stack slot or &closed)
    Value closed;                              // Heap storage cell for closed variable
    std::shared_ptr<ObjUpvalue> next{nullptr}; // Intrusive pointer for open upvalues linked list

    explicit ObjUpvalue(Value* slot)
        : location(slot), closed(), next(nullptr) {}

    [[nodiscard]] bool is_closed() const noexcept {
        return location == &closed;
    }

    /// Migrates the referenced stack value into heap storage and updates location
    void close() {
        if (!is_closed() && location != nullptr) {
            closed = *location;
            location = &closed;
        }
    }
};

/// Closure instance binding a compiled function with its captured runtime upvalues (Feature 8)
class ObjClosure {
public:
    std::shared_ptr<ObjFunction> function;
    std::vector<std::shared_ptr<ObjUpvalue>> upvalues;

    explicit ObjClosure(std::shared_ptr<ObjFunction> fn)
        : function(std::move(fn)) {
        if (function) {
            upvalues.resize(function->upvalue_descs.size(), nullptr);
        }
    }
};

} // namespace bvm

#endif // BYTECODE_VM_CLOSURE_HPP
