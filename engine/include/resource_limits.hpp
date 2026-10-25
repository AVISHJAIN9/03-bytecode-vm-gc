#ifndef BYTECODE_VM_RESOURCE_LIMITS_HPP
#define BYTECODE_VM_RESOURCE_LIMITS_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <stdexcept>
#include <chrono>

namespace bvm {

/// Base exception for VM resource budget violations (Feature 15)
class ResourceLimitException : public std::runtime_error {
public:
    explicit ResourceLimitException(const std::string& msg)
        : std::runtime_error(msg) {}
};

/// Exception thrown when instruction gas budget is exhausted
class GasExhaustedException : public ResourceLimitException {
public:
    explicit GasExhaustedException(uint64_t limit)
        : ResourceLimitException("Resource quota exceeded: Instruction gas limit of " +
                                 std::to_string(limit) + " instructions exhausted") {}
};

/// Exception thrown when heap memory quota is breached
class OutOfMemoryException : public ResourceLimitException {
public:
    explicit OutOfMemoryException(size_t requested, size_t current, size_t max_bytes)
        : ResourceLimitException("Resource quota exceeded: Out of memory (requested " +
                                 std::to_string(requested) + " bytes, currently allocated " +
                                 std::to_string(current) + " bytes, limit is " +
                                 std::to_string(max_bytes) + " bytes)") {}
};

/// Exception thrown when execution wall-clock time limit is exceeded
class TimeLimitExceededException : public ResourceLimitException {
public:
    explicit TimeLimitExceededException(uint64_t deadline_ms)
        : ResourceLimitException("Resource quota exceeded: Wall-clock time limit of " +
                                 std::to_string(deadline_ms) + " ms exceeded") {}
};

/// Resource Quota configuration and runtime budget tracker
class ResourceQuota {
public:
    uint64_t max_instructions{UINT64_MAX};
    uint64_t remaining_instructions{UINT64_MAX};
    bool gas_metering_enabled{false};

    size_t max_heap_bytes{SIZE_MAX};
    size_t allocated_heap_bytes{0};

    size_t max_call_depth{2048};

    uint64_t execution_deadline_ms{0}; // 0 = no timeout
    std::chrono::time_point<std::chrono::steady_clock> start_time;

    ResourceQuota() {
        start_time = std::chrono::steady_clock::now();
    }

    /// Sets strict instruction gas budget
    void set_instruction_budget(uint64_t instructions) noexcept {
        max_instructions = instructions;
        remaining_instructions = instructions;
        gas_metering_enabled = (instructions != UINT64_MAX);
    }

    /// Sets strict heap memory allocation limit
    void set_memory_budget(size_t bytes) noexcept {
        max_heap_bytes = bytes;
    }

    /// Sets execution time budget in milliseconds
    void set_time_budget_ms(uint64_t ms) noexcept {
        execution_deadline_ms = ms;
        start_time = std::chrono::steady_clock::now();
    }

    /// Resets the start time for the current execution run
    void start_execution_timer() noexcept {
        start_time = std::chrono::steady_clock::now();
    }

    /// Deducts instruction gas; throws GasExhaustedException on underflow
    void consume_gas(uint64_t amount = 1) {
        if (!gas_metering_enabled) return;
        if (remaining_instructions <= amount) {
            remaining_instructions = 0;
            throw GasExhaustedException(max_instructions);
        }
        remaining_instructions -= amount;
    }

    /// Checks if adding `bytes` would exceed max_heap_bytes; if so, throws OutOfMemoryException
    void track_allocation(size_t bytes) {
        if (max_heap_bytes != SIZE_MAX && (allocated_heap_bytes + bytes > max_heap_bytes)) {
            throw OutOfMemoryException(bytes, allocated_heap_bytes, max_heap_bytes);
        }
        allocated_heap_bytes += bytes;
    }

    /// Releases memory tracking
    void track_deallocation(size_t bytes) noexcept {
        if (allocated_heap_bytes >= bytes) {
            allocated_heap_bytes -= bytes;
        } else {
            allocated_heap_bytes = 0;
        }
    }

    /// Checks if wall-clock execution time exceeded deadline
    void check_time_limit() {
        if (execution_deadline_ms == 0) return;
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        if (static_cast<uint64_t>(elapsed_ms) > execution_deadline_ms) {
            throw TimeLimitExceededException(execution_deadline_ms);
        }
    }

    void reset() {
        remaining_instructions = max_instructions;
        allocated_heap_bytes = 0;
        start_time = std::chrono::steady_clock::now();
    }
};

} // namespace bvm

#endif // BYTECODE_VM_RESOURCE_LIMITS_HPP
