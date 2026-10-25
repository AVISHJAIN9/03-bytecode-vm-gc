#ifndef BYTECODE_VM_VM_HPP
#define BYTECODE_VM_VM_HPP

#include "call_frame.hpp"
#include "exceptions.hpp"
#include "debug.hpp"
#include "verifier.hpp"
#include "capabilities.hpp"
#include "resource_limits.hpp"
#include "object.hpp"
#include "allocator.hpp"
#include "ffi.hpp"
#include "root_set.hpp"
#include "safepoint.hpp"
#include "gc_mark_sweep.hpp"
#include "incremental_gc.hpp"
#include "vm_gc_hooks.hpp"
#include "handles.hpp"
#include "card_table.hpp"
#include "generational.hpp"
#include "compact.hpp"
#include "weak_ref.hpp"
#include "finalizer.hpp"
#include "gc_telemetry.hpp"
#include <vector>
#include <string>
#include <iostream>
#include <memory>
#include <array>
#include <unordered_map>

namespace bvm {

/// Execution status result from the Virtual Machine
enum class InterpretResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR
};

/// Telemetry record for opcode transition pair
struct OpcodeTransition {
    Opcode from;
    Opcode to;
    uint64_t count;
};

/// High-Performance Virtual Machine with Threaded Dispatch, Sandboxing, Quotas, and Native FFI
class VirtualMachine {
public:
    static constexpr size_t STACK_MAX = MAX_STACK_SIZE;
    static constexpr size_t FRAMES_MAX = MAX_CALL_FRAMES;

private:
    std::vector<Value> stack_;
    std::vector<CallFrame> frames_;
    std::shared_ptr<ObjUpvalue> open_upvalues_{nullptr};
    std::vector<TryHandler> try_handlers_;
    std::unordered_map<std::string, Value> globals_;
    std::string error_message_;
    bool trace_execution_{false};

    // Sandboxing, Resource Budgets & Memory Management (Features 13 - 17)
    CapabilityContext capabilities_;
    ResourceQuota quota_;
    MemoryAllocator allocator_{&quota_};
    NativeRegistry native_registry_;
    NativeHandleManager handle_manager_;

    // Garbage Collector Core & Incremental Engine (Features 19 - 24)
    SafepointMechanism safepoint_;
    MarkSweepCollector stw_gc_;
    IncrementalGC inc_gc_;

    // Advanced Memory Management, Generational, Handles & Lifecycle (Features 25 - 30)
    HandleTable handle_table_;
    WeakReferenceManager weak_ref_manager_;
    FinalizerManager finalizer_manager_;
    GenerationalCollector gen_gc_;
    GCTelemetry gc_telemetry_;

    // Telemetry and Profiling counters (Feature 12)
    std::array<uint64_t, static_cast<size_t>(Opcode::OP_COUNT)> opcode_counts_{};
    std::array<std::array<uint64_t, static_cast<size_t>(Opcode::OP_COUNT)>, static_cast<size_t>(Opcode::OP_COUNT)> opcode_transitions_{};
    uint8_t last_opcode_{static_cast<uint8_t>(Opcode::OP_NOP)};
    uint64_t total_instructions_{0};

    void runtime_error(const std::string& msg);

public:
    VirtualMachine();
    ~VirtualMachine() = default;

    void reset();
    void reset_execution();

    /// Enables or disables instruction-by-instruction execution tracing
    void set_trace(bool enable) noexcept { trace_execution_ = enable; }

    /// Stack operations
    void push(Value val);
    Value pop();
    [[nodiscard]] const Value& peek(size_t distance = 0) const;

    /// Upvalue lifecycle management (Feature 8)
    std::shared_ptr<ObjUpvalue> capture_upvalue(Value* local);
    void close_upvalues(Value* last_slot);

    /// Execution entry points
    InterpretResult interpret(const Chunk& chunk, bool verify_first = true);
    InterpretResult interpret(std::shared_ptr<ObjFunction> function, bool verify_first = true);
    InterpretResult interpret(std::shared_ptr<ObjClosure> closure);

    /// Core instruction dispatch evaluation loop (Features 7, 8, 9, 11, 13)
    InterpretResult run();

    /// Global variables
    void set_global(const std::string& name, Value val) { globals_[name] = std::move(val); }
    [[nodiscard]] const Value* get_global(const std::string& name) const {
        auto it = globals_.find(name);
        return it != globals_.end() ? &it->second : nullptr;
    }
    [[nodiscard]] const std::unordered_map<std::string, Value>& globals() const noexcept { return globals_; }

    /// Inspection and Diagnostics
    [[nodiscard]] const Value& last_stack_value() const;
    [[nodiscard]] size_t stack_size() const noexcept { return stack_.size(); }
    [[nodiscard]] size_t call_depth() const noexcept { return frames_.size(); }
    [[nodiscard]] const std::string& error_message() const noexcept { return error_message_; }
    [[nodiscard]] const std::vector<Value>& stack() const noexcept { return stack_; }
    [[nodiscard]] const std::vector<CallFrame>& frames() const noexcept { return frames_; }
    [[nodiscard]] const std::shared_ptr<ObjUpvalue>& open_upvalues() const noexcept { return open_upvalues_; }

    /// Runtime Subsystems (Features 13 - 17)
    [[nodiscard]] CapabilityContext& capabilities() noexcept { return capabilities_; }
    [[nodiscard]] const CapabilityContext& capabilities() const noexcept { return capabilities_; }
    [[nodiscard]] ResourceQuota& quota() noexcept { return quota_; }
    [[nodiscard]] const ResourceQuota& quota() const noexcept { return quota_; }
    [[nodiscard]] MemoryAllocator& allocator() noexcept { return allocator_; }
    [[nodiscard]] NativeRegistry& native_registry() noexcept { return native_registry_; }
    [[nodiscard]] const NativeRegistry& native_registry() const noexcept { return native_registry_; }
    [[nodiscard]] NativeHandleManager& handle_manager() noexcept { return handle_manager_; }
    [[nodiscard]] const NativeHandleManager& handle_manager() const noexcept { return handle_manager_; }

    uint16_t register_native(const std::string& name, NativeFn fn) {
        return native_registry_.register_fn(name, std::move(fn));
    }

    /// Garbage Collector Subsystems (Features 19 - 24)
    [[nodiscard]] SafepointMechanism& safepoint() noexcept { return safepoint_; }
    [[nodiscard]] const SafepointMechanism& safepoint() const noexcept { return safepoint_; }
    [[nodiscard]] MarkSweepCollector& stw_gc() noexcept { return stw_gc_; }
    [[nodiscard]] const MarkSweepCollector& stw_gc() const noexcept { return stw_gc_; }
    [[nodiscard]] IncrementalGC& inc_gc() noexcept { return inc_gc_; }
    [[nodiscard]] const IncrementalGC& inc_gc() const noexcept { return inc_gc_; }

    /// Advanced Memory Management & Lifecycle Subsystems (Features 25 - 30)
    [[nodiscard]] HandleTable& handle_table() noexcept { return handle_table_; }
    [[nodiscard]] const HandleTable& handle_table() const noexcept { return handle_table_; }
    [[nodiscard]] WeakReferenceManager& weak_ref_manager() noexcept { return weak_ref_manager_; }
    [[nodiscard]] const WeakReferenceManager& weak_ref_manager() const noexcept { return weak_ref_manager_; }
    [[nodiscard]] FinalizerManager& finalizer_manager() noexcept { return finalizer_manager_; }
    [[nodiscard]] const FinalizerManager& finalizer_manager() const noexcept { return finalizer_manager_; }
    [[nodiscard]] GenerationalCollector& gen_gc() noexcept { return gen_gc_; }
    [[nodiscard]] const GenerationalCollector& gen_gc() const noexcept { return gen_gc_; }
    [[nodiscard]] GCTelemetry& gc_telemetry() noexcept { return gc_telemetry_; }
    [[nodiscard]] const GCTelemetry& gc_telemetry() const noexcept { return gc_telemetry_; }

    /// Triggers a full STW GC collection
    size_t collect_garbage() {
        return stw_gc_.collect_garbage(*this);
    }

    /// Telemetry & Profiling APIs (Feature 12)
    void reset_telemetry() noexcept;
    [[nodiscard]] uint64_t opcode_count(Opcode op) const noexcept;
    [[nodiscard]] uint64_t total_instructions() const noexcept { return total_instructions_; }
    [[nodiscard]] std::vector<OpcodeTransition> top_transitions(size_t k = 10) const;
    void dump_telemetry(std::ostream& out) const;
};

} // namespace bvm

#endif // BYTECODE_VM_VM_HPP
