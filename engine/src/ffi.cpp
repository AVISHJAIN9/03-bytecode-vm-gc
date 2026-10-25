#include "ffi.hpp"
#include "resource_limits.hpp"

namespace bvm {

uint16_t NativeRegistry::register_fn(const std::string& name, NativeFn fn) {
    auto it = functions_by_name_.find(name);
    if (it != functions_by_name_.end()) {
        uint16_t idx = get_index(name);
        function_table_[idx] = std::move(fn);
        functions_by_name_[name] = function_table_[idx];
        return idx;
    }

    if (function_table_.size() >= 0xFFFF) {
        throw std::overflow_error("Native function table exceeded limit of 65535 functions");
    }

    uint16_t idx = static_cast<uint16_t>(function_table_.size());
    function_names_.push_back(name);
    function_table_.push_back(fn);
    functions_by_name_[name] = std::move(fn);
    return idx;
}

bool NativeRegistry::has_fn(const std::string& name) const noexcept {
    return functions_by_name_.find(name) != functions_by_name_.end();
}

uint16_t NativeRegistry::get_index(const std::string& name) const {
    for (size_t i = 0; i < function_names_.size(); ++i) {
        if (function_names_[i] == name) {
            return static_cast<uint16_t>(i);
        }
    }
    throw std::out_of_range("Native function not registered: '" + name + "'");
}

const NativeFn& NativeRegistry::get_by_index(uint16_t index) const {
    if (index >= function_table_.size()) {
        throw std::out_of_range("Native function index out of bounds: " + std::to_string(index));
    }
    return function_table_[index];
}

const std::string& NativeRegistry::get_name_by_index(uint16_t index) const {
    if (index >= function_names_.size()) {
        throw std::out_of_range("Native function index out of bounds: " + std::to_string(index));
    }
    return function_names_[index];
}

Value NativeRegistry::invoke(uint16_t index, NativeContext& ctx) const {
    if (index >= function_table_.size()) {
        throw std::out_of_range("Native function index out of bounds: " + std::to_string(index));
    }
    const auto& name = function_names_[index];
    const auto& fn = function_table_[index];

    try {
        return fn(ctx);
    } catch (const SecurityException&) {
        throw; // Propagate security violations directly to VM runtime handler
    } catch (const ResourceLimitException&) {
        throw; // Propagate quota breaches directly
    } catch (const std::exception& e) {
        throw std::runtime_error("Native call panic in '" + name + "': " + e.what());
    } catch (...) {
        throw std::runtime_error("Unknown unhandled native panic in '" + name + "'");
    }
}

Value NativeRegistry::invoke(const std::string& name, NativeContext& ctx) const {
    uint16_t idx = get_index(name);
    return invoke(idx, ctx);
}

} // namespace bvm
