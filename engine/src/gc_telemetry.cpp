#include "gc_telemetry.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace bvm {

GCTelemetry::GCTelemetry() {
    pause_samples_ns_.reserve(2048);
}

void GCTelemetry::record_minor_gc(uint64_t pause_ns, size_t bytes_reclaimed, size_t bytes_promoted, size_t dirty_cards) {
    minor_gcs_++;
    bytes_reclaimed_ += bytes_reclaimed;
    bytes_promoted_ += bytes_promoted;
    dirty_cards_scanned_ += dirty_cards;

    pause_samples_ns_.push_back(pause_ns);
    total_pause_ns_ += pause_ns;
    max_pause_ns_ = std::max(max_pause_ns_, pause_ns);
}

void GCTelemetry::record_major_gc(uint64_t pause_ns, size_t bytes_reclaimed) {
    major_gcs_++;
    bytes_reclaimed_ += bytes_reclaimed;

    pause_samples_ns_.push_back(pause_ns);
    total_pause_ns_ += pause_ns;
    max_pause_ns_ = std::max(max_pause_ns_, pause_ns);
}

void GCTelemetry::record_incremental_step(uint64_t pause_ns) {
    incremental_steps_++;
    pause_samples_ns_.push_back(pause_ns);
    total_pause_ns_ += pause_ns;
    max_pause_ns_ = std::max(max_pause_ns_, pause_ns);
}

void GCTelemetry::record_compaction(uint64_t pause_ns, size_t bytes_compacted, size_t objects_pinned) {
    compactions_++;
    bytes_reclaimed_ += bytes_compacted;
    objects_pinned_total_ += objects_pinned;

    pause_samples_ns_.push_back(pause_ns);
    total_pause_ns_ += pause_ns;
    max_pause_ns_ = std::max(max_pause_ns_, pause_ns);
}

uint64_t GCTelemetry::p50_pause_ns() const {
    if (pause_samples_ns_.empty()) return 0;
    auto sorted = pause_samples_ns_;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = sorted.size() * 50 / 100;
    return sorted[idx];
}

uint64_t GCTelemetry::p95_pause_ns() const {
    if (pause_samples_ns_.empty()) return 0;
    auto sorted = pause_samples_ns_;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = sorted.size() * 95 / 100;
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
}

uint64_t GCTelemetry::p99_pause_ns() const {
    if (pause_samples_ns_.empty()) return 0;
    auto sorted = pause_samples_ns_;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = sorted.size() * 99 / 100;
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
}

double GCTelemetry::metrics_coverage() const noexcept {
    // Check coverage of primary telemetry indicators
    int active_indicators = 0;
    constexpr int total_indicators = 10;

    if (minor_gcs_ > 0 || major_gcs_ > 0) active_indicators++;
    if (compactions_ > 0) active_indicators++;
    if (bytes_allocated_ > 0) active_indicators++;
    if (bytes_reclaimed_ > 0) active_indicators++;
    if (bytes_promoted_ > 0) active_indicators++;
    if (dirty_cards_scanned_ > 0) active_indicators++;
    if (current_nursery_utilization_ >= 0.0) active_indicators++;
    if (heap_fragmentation_ratio_ >= 0.0) active_indicators++;
    if (!pause_samples_ns_.empty()) active_indicators++;
    if (total_pause_ns_ > 0 || !pause_samples_ns_.empty()) active_indicators++;

    return (static_cast<double>(active_indicators) / static_cast<double>(total_indicators)) * 100.0;
}

void GCTelemetry::print_summary(std::ostream& os) const {
    os << "=======================================================\n";
    os << "           GARBAGE COLLECTOR TELEMETRY REPORT          \n";
    os << "=======================================================\n";
    os << "Cycle Counters:\n";
    os << "  Minor GCs (Nursery)       : " << minor_gcs_ << "\n";
    os << "  Major GCs (STW/Full)      : " << major_gcs_ << "\n";
    os << "  Incremental Steps         : " << incremental_steps_ << "\n";
    os << "  Sliding Compactions       : " << compactions_ << "\n";
    os << "\nMemory Volumes:\n";
    os << "  Bytes Allocated           : " << bytes_allocated_ << " B\n";
    os << "  Bytes Reclaimed           : " << bytes_reclaimed_ << " B\n";
    os << "  Bytes Promoted (Tenured)  : " << bytes_promoted_ << " B\n";
    os << "  Dirty Cards Scanned       : " << dirty_cards_scanned_ << "\n";
    os << "\nPause Latency Distribution:\n";
    os << "  Total Pause Time          : " << total_pause_ns_ / 1000.0 << " us\n";
    os << "  p50 Pause Time            : " << p50_pause_ns() / 1000.0 << " us\n";
    os << "  p95 Pause Time            : " << p95_pause_ns() / 1000.0 << " us\n";
    os << "  p99 Pause Time            : " << p99_pause_ns() / 1000.0 << " us\n";
    os << "  Max Pause Time            : " << max_pause_ns_ / 1000.0 << " us\n";
    os << "\nHeap Utilization & Fragmentation:\n";
    os << "  Nursery Utilization       : " << std::fixed << std::setprecision(2)
       << (current_nursery_utilization_ * 100.0) << " %\n";
    os << "  Heap Fragmentation Ratio  : " << (heap_fragmentation_ratio_ * 100.0) << " %\n";
    os << "  Metrics Coverage          : " << metrics_coverage() << " %\n";
    os << "=======================================================\n";
}

std::string GCTelemetry::to_json() const {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"minor_gcs\": " << minor_gcs_ << ",\n";
    ss << "  \"major_gcs\": " << major_gcs_ << ",\n";
    ss << "  \"incremental_steps\": " << incremental_steps_ << ",\n";
    ss << "  \"compactions\": " << compactions_ << ",\n";
    ss << "  \"bytes_allocated\": " << bytes_allocated_ << ",\n";
    ss << "  \"bytes_reclaimed\": " << bytes_reclaimed_ << ",\n";
    ss << "  \"bytes_promoted\": " << bytes_promoted_ << ",\n";
    ss << "  \"dirty_cards_scanned\": " << dirty_cards_scanned_ << ",\n";
    ss << "  \"objects_pinned\": " << objects_pinned_total_ << ",\n";
    ss << "  \"weak_refs_cleared\": " << weak_refs_cleared_total_ << ",\n";
    ss << "  \"finalizers_executed\": " << finalizers_executed_total_ << ",\n";
    ss << "  \"total_pause_ns\": " << total_pause_ns_ << ",\n";
    ss << "  \"p50_pause_ns\": " << p50_pause_ns() << ",\n";
    ss << "  \"p95_pause_ns\": " << p95_pause_ns() << ",\n";
    ss << "  \"p99_pause_ns\": " << p99_pause_ns() << ",\n";
    ss << "  \"max_pause_ns\": " << max_pause_ns_ << ",\n";
    ss << "  \"nursery_utilization\": " << current_nursery_utilization_ << ",\n";
    ss << "  \"heap_fragmentation_ratio\": " << heap_fragmentation_ratio_ << ",\n";
    ss << "  \"metrics_coverage\": " << metrics_coverage() << "\n";
    ss << "}";
    return ss.str();
}

void GCTelemetry::reset() noexcept {
    minor_gcs_ = 0;
    major_gcs_ = 0;
    incremental_steps_ = 0;
    compactions_ = 0;
    bytes_allocated_ = 0;
    bytes_reclaimed_ = 0;
    bytes_promoted_ = 0;
    dirty_cards_scanned_ = 0;
    objects_pinned_total_ = 0;
    weak_refs_cleared_total_ = 0;
    finalizers_executed_total_ = 0;
    current_nursery_utilization_ = 0.0;
    heap_fragmentation_ratio_ = 0.0;
    pause_samples_ns_.clear();
    total_pause_ns_ = 0;
    max_pause_ns_ = 0;
}

} // namespace bvm
