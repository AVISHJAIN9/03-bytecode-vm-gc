#ifndef BYTECODE_VM_FFI_HPP
#define BYTECODE_VM_FFI_HPP

#include "value.hpp"
#include "object.hpp"
#include "capabilities.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <stdexcept>

namespace bvm {

class VirtualMachine;

/// Opaque Native Handle wrapper providing crash-proof pointer safety (Feature 13)
template <typename T>
class NativeHandle {
private:
    T* ptr_{nullptr};
    std::string type_name_;

public:
    NativeHandle(T* ptr, std::string type)
        : ptr_(ptr), type_name_(std::move(type)) {}

    [[nodiscard]] T* get() const noexcept { return ptr_; }
    [[nodiscard]] const std::string& type_name() const noexcept { return type_name_; }
    [[nodiscard]] bool is_valid() const noexcept { return ptr_ != nullptr; }

    T* operator->() const {
        if (!ptr_) throw std::runtime_error("Dereference of null NativeHandle [" + type_name_ + "]");
        return ptr_;
    }

    T& operator*() const {
        if (!ptr_) throw std::runtime_error("Dereference of null NativeHandle [" + type_name_ + "]");
        return *ptr_;
    }
};

/// Registry tracking active native handles for GC root tracing and lifecycle management
class NativeHandleManager {
private:
    std::vector<ObjNativeHandle*> active_handles_;

public:
    NativeHandleManager() = default;

    void register_handle(ObjNativeHandle* handle) {
        if (handle) {
            active_handles_.push_back(handle);
        }
    }

    void unregister_handle(ObjNativeHandle* handle) {
        for (auto it = active_handles_.begin(); it != active_handles_.end(); ++it) {
            if (*it == handle) {
                active_handles_.erase(it);
                break;
            }
        }
    }

    [[nodiscard]] const std::vector<ObjNativeHandle*>& active_handles() const noexcept {
        return active_handles_;
    }

    void clear() noexcept {
        active_handles_.clear();
    }
};

/// Execution Context passed to Native Functions for safe argument marshalling and sandboxing
class NativeContext {
private:
    VirtualMachine& vm_;
    CapabilityContext& capabilities_;
    const Value* args_{nullptr};
    size_t argc_{0};

public:
    NativeContext(VirtualMachine& vm, CapabilityContext& caps, const Value* args, size_t argc)
        : vm_(vm), capabilities_(caps), args_(args), argc_(argc) {}

    [[nodiscard]] size_t arg_count() const noexcept { return argc_; }
    [[nodiscard]] VirtualMachine& vm() noexcept { return vm_; }
    [[nodiscard]] CapabilityContext& capabilities() noexcept { return capabilities_; }

    /// Enforces capability permission; throws SecurityException on sandbox violation
    void require_capability(Capability cap) const {
        capabilities_.require(cap);
    }

    /// Parameter Marshalling with bounds and type verification
    [[nodiscard]] const Value& get_arg(size_t index) const {
        if (index >= argc_) {
            throw std::out_of_range("FFI argument index " + std::to_string(index) +
                                    " out of bounds (argc: " + std::to_string(argc_) + ")");
        }
        return args_[index];
    }

    [[nodiscard]] int64_t get_int(size_t index) const {
        const auto& val = get_arg(index);
        return val.as_int();
    }

    [[nodiscard]] double get_float(size_t index) const {
        const auto& val = get_arg(index);
        return val.as_number();
    }

    [[nodiscard]] std::string get_string(size_t index) const {
        const auto& val = get_arg(index);
        if (val.is_string()) {
            return val.as_string();
        }
        if (val.is_object() && val.as_object()->obj_type() == ObjType::STRING) {
            auto* s = reinterpret_cast<ObjString*>(val.as_object());
            return std::string(s->view());
        }
        throw std::runtime_error("FFI type error: expected string at argument " + std::to_string(index));
    }

    [[nodiscard]] bool get_bool(size_t index) const {
        const auto& val = get_arg(index);
        return val.as_bool();
    }

    [[nodiscard]] ObjHeader* get_object(size_t index) const {
        const auto& val = get_arg(index);
        return val.as_object();
    }

    template <typename T>
    [[nodiscard]] T* get_handle(size_t index, const std::string& expected_type) const {
        const auto& val = get_arg(index);
        if (!val.is_object() || val.as_object()->obj_type() != ObjType::NATIVE_HANDLE) {
            throw std::runtime_error("FFI type error: expected NativeHandle [" + expected_type +
                                    "] at argument " + std::to_string(index));
        }
        auto* handle = reinterpret_cast<ObjNativeHandle*>(val.as_object());
        if (handle->type_name != expected_type) {
            throw std::runtime_error("FFI type error: handle type mismatch (expected '" +
                                    expected_type + "', got '" + handle->type_name + "')");
        }
        return static_cast<T*>(handle->handle_ptr);
    }
};

/// Type definition for safe native C++ functions callable from bytecode
using NativeFn = std::function<Value(NativeContext&)>;

/// Global / VM-level Native Function Registry
class NativeRegistry {
private:
    std::unordered_map<std::string, NativeFn> functions_by_name_;
    std::vector<std::string> function_names_;
    std::vector<NativeFn> function_table_;

public:
    NativeRegistry() = default;

    /// Registers a native function and returns its assigned 16-bit table index
    uint16_t register_fn(const std::string& name, NativeFn fn);

    [[nodiscard]] bool has_fn(const std::string& name) const noexcept;
    [[nodiscard]] uint16_t get_index(const std::string& name) const;
    [[nodiscard]] const NativeFn& get_by_index(uint16_t index) const;
    [[nodiscard]] const std::string& get_name_by_index(uint16_t index) const;

    /// Invokes native function with crash-proof isolation barrier
    Value invoke(uint16_t index, NativeContext& ctx) const;
    Value invoke(const std::string& name, NativeContext& ctx) const;
};

} // namespace bvm

#endif // BYTECODE_VM_FFI_HPP
