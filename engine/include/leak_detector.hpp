#ifndef BYTECODE_VM_LEAK_DETECTOR_HPP
#define BYTECODE_VM_LEAK_DETECTOR_HPP

#include "object.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <iostream>

namespace bvm {

class VirtualMachine;

/// Node descriptor in the Dominator Tree (Feature 32)
struct DominatorNode {
    size_t index{0};
    ObjHeader* obj{nullptr};
    std::string label;
    size_t self_size{0};
    size_t retained_size{0};
    size_t idom_index{0};
    std::vector<size_t> dominated_children;
};

/// Dominator Tree & Memory Retention Analyzer (Feature 32)
/// Implements exact graph dominator tree construction to detect memory leak roots and compute retained sizes
class DominatorTreeAnalyzer {
private:
    std::vector<DominatorNode> nodes_;
    std::unordered_map<ObjHeader*, size_t> obj_to_node_map_;
    std::vector<std::vector<size_t>> adj_;
    std::vector<std::vector<size_t>> pred_;
    size_t super_root_{0};

    void build_graph(VirtualMachine& vm);
    void compute_dominators();
    void compute_retained_sizes();

public:
    DominatorTreeAnalyzer() = default;

    /// Analyzes the current VM heap and constructs the complete dominator tree
    static DominatorTreeAnalyzer analyze(VirtualMachine& vm);

    [[nodiscard]] const std::vector<DominatorNode>& nodes() const noexcept { return nodes_; }
    [[nodiscard]] const DominatorNode* get_node(ObjHeader* obj) const noexcept;

    /// Finds the shortest retention path from a GC root to the target object
    [[nodiscard]] std::vector<std::string> find_shortest_retention_path(ObjHeader* target) const;

    /// Returns top N objects ranked by retained memory size (leak candidates)
    [[nodiscard]] std::vector<DominatorNode> get_top_retained_objects(size_t limit = 10) const;

    /// Formats a diagnostic leak report identifying the top retained objects and retention paths
    void dump_leak_report(std::ostream& os, size_t limit = 10) const;
};

} // namespace bvm

#endif // BYTECODE_VM_LEAK_DETECTOR_HPP
