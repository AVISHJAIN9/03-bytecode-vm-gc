#ifndef BYTECODE_VM_HEAP_SNAPSHOT_HPP
#define BYTECODE_VM_HEAP_SNAPSHOT_HPP

#include "object.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

namespace bvm {

class VirtualMachine;

/// V8-Compatible Heap Snapshot Node Types (Feature 31)
enum class SnapshotNodeType : uint8_t {
    HIDDEN    = 0,
    ARRAY     = 1,
    STRING    = 2,
    OBJECT    = 3,
    CODE      = 4,
    CLOSURE   = 5,
    REGEXP    = 6,
    NUMBER    = 7,
    NATIVE    = 8,
    SYNTHETIC = 9
};

/// V8-Compatible Heap Snapshot Edge Types (Feature 31)
enum class SnapshotEdgeType : uint8_t {
    CONTEXT   = 0,
    ELEMENT   = 1,
    PROPERTY  = 2,
    INTERNAL  = 3,
    HIDDEN    = 4,
    SHORTCUT  = 5,
    WEAK      = 6
};

/// V8-Compatible Heap Snapshot Exporter (Feature 31)
/// Emits full graph-accurate heap snapshots compatible with Chrome DevTools Memory Inspector
class HeapSnapshotExporter {
public:
    struct SnapshotStats {
        size_t node_count{0};
        size_t edge_count{0};
        size_t string_count{0};
        size_t total_size_bytes{0};
    };

    /// Generates full V8-compatible .heapsnapshot JSON string representing current VM heap
    static std::string export_snapshot_json(VirtualMachine& vm);

    /// Analyzes heap graph metrics without full JSON serialization
    static SnapshotStats analyze_snapshot(VirtualMachine& vm);
};

} // namespace bvm

#endif // BYTECODE_VM_HEAP_SNAPSHOT_HPP
