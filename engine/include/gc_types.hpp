#ifndef BYTECODE_VM_GC_TYPES_HPP
#define BYTECODE_VM_GC_TYPES_HPP

#include "object.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>

namespace bvm {

/// High-level GC Phase State Machine (Feature 24)
enum class GCPhase : uint8_t {
    IDLE = 0,
    ROOT_SCAN = 1,
    MARKING = 2,
    SWEEPING = 3
};

inline const char* to_string(GCPhase phase) noexcept {
    switch (phase) {
        case GCPhase::IDLE: return "IDLE";
        case GCPhase::ROOT_SCAN: return "ROOT_SCAN";
        case GCPhase::MARKING: return "MARKING";
        case GCPhase::SWEEPING: return "SWEEPING";
    }
    return "UNKNOWN";
}

/// Explicit Gray Worklist Stack (Feature 19 & 21)
/// Avoids C++ native call stack overflow when traversing arbitrary deep/cyclic object graphs
class GrayWorklist {
private:
    std::vector<ObjHeader*> stack_;

public:
    GrayWorklist() {
        stack_.reserve(1024);
    }

    void push(ObjHeader* obj) {
        if (!obj) return;
        stack_.push_back(obj);
    }

    [[nodiscard]] ObjHeader* pop() noexcept {
        if (stack_.empty()) return nullptr;
        ObjHeader* top = stack_.back();
        stack_.pop_back();
        return top;
    }

    [[nodiscard]] bool empty() const noexcept {
        return stack_.empty();
    }

    [[nodiscard]] size_t size() const noexcept {
        return stack_.size();
    }

    void clear() noexcept {
        stack_.clear();
    }

    [[nodiscard]] const std::vector<ObjHeader*>& raw_stack() const noexcept {
        return stack_;
    }
};

/// Collector Performance & Telemetry Tracker (Features 19 - 24)
struct GCStats {
    size_t total_collections{0};
    size_t stw_collections{0};
    size_t incremental_steps{0};

    size_t bytes_allocated_before{0};
    size_t bytes_allocated_after{0};
    size_t total_bytes_reclaimed{0};

    size_t objects_freed_total{0};
    size_t objects_survived_last_cycle{0};

    size_t write_barriers_triggered{0};

    uint64_t total_pause_time_us{0};
    uint64_t max_pause_time_us{0};
    uint64_t last_pause_time_us{0};

    void record_pause(uint64_t us) noexcept {
        last_pause_time_us = us;
        total_pause_time_us += us;
        max_pause_time_us = std::max(max_pause_time_us, us);
    }

    void reset() noexcept {
        total_collections = 0;
        stw_collections = 0;
        incremental_steps = 0;
        bytes_allocated_before = 0;
        bytes_allocated_after = 0;
        total_bytes_reclaimed = 0;
        objects_freed_total = 0;
        objects_survived_last_cycle = 0;
        write_barriers_triggered = 0;
        total_pause_time_us = 0;
        max_pause_time_us = 0;
        last_pause_time_us = 0;
    }
};

} // namespace bvm

#endif // BYTECODE_VM_GC_TYPES_HPP
