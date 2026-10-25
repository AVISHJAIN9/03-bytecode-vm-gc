#ifndef BYTECODE_VM_GC_TELEMETRY_HPP
#define BYTECODE_VM_GC_TELEMETRY_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>

namespace bvm {

/// Real-Time Garbage Collector Telemetry & Diagnostics (Feature 30)
class GCTelemetry {
private:
    uint64_t minor_gcs_{0};
    uint64_t major_gcs_{0};
    uint64_t incremental_steps_{0};
    uint64_t compactions_{0};

    size_t bytes_allocated_{0};
    size_t bytes_reclaimed_{0};
    size_t bytes_promoted_{0};
    size_t dirty_cards_scanned_{0};

    size_t objects_pinned_total_{0};
    size_t weak_refs_cleared_total_{0};
    size_t finalizers_executed_total_{0};

    double current_nursery_utilization_{0.0};
    double heap_fragmentation_ratio_{0.0};

    std::vector<uint64_t> pause_samples_ns_;
    uint64_t total_pause_ns_{0};
    uint64_t max_pause_ns_{0};

public:
    GCTelemetry();
    ~GCTelemetry() = default;

    void record_minor_gc(uint64_t pause_ns, size_t bytes_reclaimed, size_t bytes_promoted, size_t dirty_cards);
    void record_major_gc(uint64_t pause_ns, size_t bytes_reclaimed);
    void record_incremental_step(uint64_t pause_ns);
    void record_compaction(uint64_t pause_ns, size_t bytes_compacted, size_t objects_pinned);
    void record_allocation(size_t bytes) noexcept { bytes_allocated_ += bytes; }

    void update_nursery_utilization(double util) noexcept { current_nursery_utilization_ = util; }
    void update_fragmentation_ratio(double ratio) noexcept { heap_fragmentation_ratio_ = ratio; }
    void record_weak_cleared(size_t count) noexcept { weak_refs_cleared_total_ += count; }
    void record_finalizer_run(size_t count) noexcept { finalizers_executed_total_ += count; }

    [[nodiscard]] uint64_t minor_gcs() const noexcept { return minor_gcs_; }
    [[nodiscard]] uint64_t major_gcs() const noexcept { return major_gcs_; }
    [[nodiscard]] uint64_t incremental_steps() const noexcept { return incremental_steps_; }
    [[nodiscard]] uint64_t compactions() const noexcept { return compactions_; }

    [[nodiscard]] size_t bytes_allocated() const noexcept { return bytes_allocated_; }
    [[nodiscard]] size_t bytes_reclaimed() const noexcept { return bytes_reclaimed_; }
    [[nodiscard]] size_t bytes_promoted() const noexcept { return bytes_promoted_; }
    [[nodiscard]] size_t dirty_cards_scanned() const noexcept { return dirty_cards_scanned_; }

    [[nodiscard]] size_t objects_pinned_total() const noexcept { return objects_pinned_total_; }
    [[nodiscard]] size_t weak_refs_cleared_total() const noexcept { return weak_refs_cleared_total_; }
    [[nodiscard]] size_t finalizers_executed_total() const noexcept { return finalizers_executed_total_; }

    [[nodiscard]] double current_nursery_utilization() const noexcept { return current_nursery_utilization_; }
    [[nodiscard]] double heap_fragmentation_ratio() const noexcept { return heap_fragmentation_ratio_; }

    [[nodiscard]] uint64_t total_pause_ns() const noexcept { return total_pause_ns_; }
    [[nodiscard]] uint64_t max_pause_ns() const noexcept { return max_pause_ns_; }

    [[nodiscard]] uint64_t p50_pause_ns() const;
    [[nodiscard]] uint64_t p95_pause_ns() const;
    [[nodiscard]] uint64_t p99_pause_ns() const;

    /// Evaluates metrics coverage against defined KPI fields (> 95%)
    [[nodiscard]] double metrics_coverage() const noexcept;

    /// Dumps a formatted ASCII telemetry summary
    void print_summary(std::ostream& os) const;

    /// Exports structured telemetry in JSON format
    [[nodiscard]] std::string to_json() const;

    void reset() noexcept;
};

} // namespace bvm

#endif // BYTECODE_VM_GC_TELEMETRY_HPP
