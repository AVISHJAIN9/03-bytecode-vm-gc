#include "heap_snapshot.hpp"
#include "vm.hpp"
#include "root_set.hpp"
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>

namespace bvm {

namespace {

struct InternalNode {
    uint8_t type{static_cast<uint8_t>(SnapshotNodeType::OBJECT)};
    std::string name;
    uint64_t id{0};
    uint32_t self_size{0};
    uint32_t edge_count{0};
    ObjHeader* obj{nullptr};
};

struct InternalEdge {
    uint8_t type{static_cast<uint8_t>(SnapshotEdgeType::PROPERTY)};
    std::string name;
    uint32_t index{0};
    bool is_index{false};
    size_t to_node_idx{0};
};

class SnapshotBuilder {
private:
    std::vector<InternalNode> nodes_;
    std::vector<InternalEdge> edges_;
    std::vector<std::string> strings_;
    std::unordered_map<std::string, uint32_t> string_index_map_;
    std::unordered_map<ObjHeader*, size_t> obj_to_node_map_;

public:
    SnapshotBuilder() {
        // String table index 0 is always empty or root
        get_string_id("");
    }

    uint32_t get_string_id(const std::string& str) {
        auto it = string_index_map_.find(str);
        if (it != string_index_map_.end()) {
            return it->second;
        }
        uint32_t idx = static_cast<uint32_t>(strings_.size());
        strings_.push_back(str);
        string_index_map_[str] = idx;
        return idx;
    }

    void build(VirtualMachine& vm) {
        // 1. Synthetic SuperRoot node at index 0
        InternalNode root_node;
        root_node.type = static_cast<uint8_t>(SnapshotNodeType::SYNTHETIC);
        root_node.name = "(root)";
        root_node.id = 1;
        root_node.self_size = 0;
        root_node.obj = nullptr;
        nodes_.push_back(root_node);

        // 2. Discover all live objects
        uint64_t next_id = 3;
        ObjHeader* curr = vm.allocator().all_objects();
        while (curr) {
            InternalNode node;
            node.id = next_id;
            next_id += 2;
            node.self_size = curr->size_bytes;
            node.obj = curr;

            switch (curr->obj_type()) {
                case ObjType::STRING: {
                    node.type = static_cast<uint8_t>(SnapshotNodeType::STRING);
                    auto* s = reinterpret_cast<ObjString*>(curr);
                    node.name = std::string(s->view());
                    break;
                }
                case ObjType::ARRAY: {
                    node.type = static_cast<uint8_t>(SnapshotNodeType::ARRAY);
                    node.name = "Array";
                    break;
                }
                case ObjType::INSTANCE: {
                    node.type = static_cast<uint8_t>(SnapshotNodeType::OBJECT);
                    auto* inst = reinterpret_cast<ObjInstance*>(curr);
                    node.name = inst->class_name.empty() ? "Object" : inst->class_name;
                    break;
                }
                case ObjType::NATIVE_HANDLE: {
                    node.type = static_cast<uint8_t>(SnapshotNodeType::NATIVE);
                    auto* h = reinterpret_cast<ObjNativeHandle*>(curr);
                    node.name = "NativeHandle<" + h->type_name + ">";
                    break;
                }
                case ObjType::WEAK_REF: {
                    node.type = static_cast<uint8_t>(SnapshotNodeType::OBJECT);
                    node.name = "WeakRef";
                    break;
                }
            }

            size_t node_idx = nodes_.size();
            nodes_.push_back(node);
            obj_to_node_map_[curr] = node_idx;
            curr = curr->next;
        }

        // Also index nursery objects if present
        for (ObjHeader* nobj : vm.gen_gc().nursery().allocated_objects()) {
            if (nobj && obj_to_node_map_.find(nobj) == obj_to_node_map_.end()) {
                InternalNode node;
                node.id = next_id;
                next_id += 2;
                node.self_size = nobj->size_bytes;
                node.obj = nobj;
                node.type = static_cast<uint8_t>(SnapshotNodeType::OBJECT);
                node.name = "NurseryObject";
                size_t node_idx = nodes_.size();
                nodes_.push_back(node);
                obj_to_node_map_[nobj] = node_idx;
            }
        }

        // 3. Connect Roots from SuperRoot (Node 0)
        // Global variables -> Root edges
        for (const auto& [name, val] : vm.globals()) {
            if (val.is_object() && val.as_object() != nullptr) {
                auto it = obj_to_node_map_.find(val.as_object());
                if (it != obj_to_node_map_.end()) {
                    InternalEdge edge;
                    edge.type = static_cast<uint8_t>(SnapshotEdgeType::PROPERTY);
                    edge.name = name;
                    edge.is_index = false;
                    edge.to_node_idx = it->second;
                    edges_.push_back(edge);
                    nodes_[0].edge_count++;
                }
            }
        }

        // Stack slots -> Root edges
        for (size_t s = 0; s < vm.stack().size(); ++s) {
            const auto& val = vm.stack()[s];
            if (val.is_object() && val.as_object() != nullptr) {
                auto it = obj_to_node_map_.find(val.as_object());
                if (it != obj_to_node_map_.end()) {
                    InternalEdge edge;
                    edge.type = static_cast<uint8_t>(SnapshotEdgeType::CONTEXT);
                    edge.name = "stack_" + std::to_string(s);
                    edge.is_index = false;
                    edge.to_node_idx = it->second;
                    edges_.push_back(edge);
                    nodes_[0].edge_count++;
                }
            }
        }

        // Active Handles -> Root edges
        for (ObjNativeHandle* handle : vm.handle_manager().active_handles()) {
            if (handle) {
                auto it = obj_to_node_map_.find(&handle->header);
                if (it != obj_to_node_map_.end()) {
                    InternalEdge edge;
                    edge.type = static_cast<uint8_t>(SnapshotEdgeType::INTERNAL);
                    edge.name = "native_handle_" + handle->type_name;
                    edge.is_index = false;
                    edge.to_node_idx = it->second;
                    edges_.push_back(edge);
                    nodes_[0].edge_count++;
                }
            }
        }

        // HandleTable handles -> Root edges
        std::vector<ObjHeader*> handle_roots;
        vm.handle_table().enumerate_roots(handle_roots);
        for (ObjHeader* hr : handle_roots) {
            auto it = obj_to_node_map_.find(hr);
            if (it != obj_to_node_map_.end()) {
                InternalEdge edge;
                edge.type = static_cast<uint8_t>(SnapshotEdgeType::INTERNAL);
                edge.name = "handle_table_root";
                edge.is_index = false;
                edge.to_node_idx = it->second;
                edges_.push_back(edge);
                nodes_[0].edge_count++;
            }
        }

        // 4. Connect Inter-Object Reference Edges
        for (size_t i = 1; i < nodes_.size(); ++i) {
            ObjHeader* obj = nodes_[i].obj;
            if (!obj) continue;

            switch (obj->obj_type()) {
                case ObjType::STRING:
                case ObjType::NATIVE_HANDLE:
                    break;

                case ObjType::ARRAY: {
                    auto* arr = reinterpret_cast<ObjArray*>(obj);
                    for (size_t elem_idx = 0; elem_idx < arr->elements.size(); ++elem_idx) {
                        const auto& elem = arr->elements[elem_idx];
                        if (elem.is_object() && elem.as_object() != nullptr) {
                            auto it = obj_to_node_map_.find(elem.as_object());
                            if (it != obj_to_node_map_.end()) {
                                InternalEdge edge;
                                edge.type = static_cast<uint8_t>(SnapshotEdgeType::ELEMENT);
                                edge.index = static_cast<uint32_t>(elem_idx);
                                edge.is_index = true;
                                edge.to_node_idx = it->second;
                                edges_.push_back(edge);
                                nodes_[i].edge_count++;
                            }
                        }
                    }
                    break;
                }

                case ObjType::INSTANCE: {
                    auto* inst = reinterpret_cast<ObjInstance*>(obj);
                    for (const auto& field : inst->fields) {
                        if (field.second.is_object() && field.second.as_object() != nullptr) {
                            auto it = obj_to_node_map_.find(field.second.as_object());
                            if (it != obj_to_node_map_.end()) {
                                InternalEdge edge;
                                edge.type = static_cast<uint8_t>(SnapshotEdgeType::PROPERTY);
                                edge.name = field.first;
                                edge.is_index = false;
                                edge.to_node_idx = it->second;
                                edges_.push_back(edge);
                                nodes_[i].edge_count++;
                            }
                        }
                    }
                    break;
                }

                case ObjType::WEAK_REF: {
                    auto* wr = reinterpret_cast<ObjWeakRef*>(obj);
                    if (wr->referent != nullptr) {
                        auto it = obj_to_node_map_.find(wr->referent);
                        if (it != obj_to_node_map_.end()) {
                            InternalEdge edge;
                            edge.type = static_cast<uint8_t>(SnapshotEdgeType::WEAK);
                            edge.name = "referent";
                            edge.is_index = false;
                            edge.to_node_idx = it->second;
                            edges_.push_back(edge);
                            nodes_[i].edge_count++;
                        }
                    }
                    break;
                }
            }
        }
    }

    std::string to_json() {
        std::ostringstream ss;
        ss << "{\n";

        // 1. Snapshot Metadata
        ss << "  \"snapshot\": {\n";
        ss << "    \"meta\": {\n";
        ss << "      \"node_fields\": [\"type\", \"name\", \"id\", \"self_size\", \"edge_count\", \"trace_node_id\"],\n";
        ss << "      \"node_types\": [\n";
        ss << "        [\"hidden\", \"array\", \"string\", \"object\", \"code\", \"closure\", \"regexp\", \"number\", \"native\", \"synthetic\"],\n";
        ss << "        \"string\", \"number\", \"number\", \"number\", \"number\"\n";
        ss << "      ],\n";
        ss << "      \"edge_fields\": [\"type\", \"name_or_index\", \"to_node\"],\n";
        ss << "      \"edge_types\": [\n";
        ss << "        [\"context\", \"element\", \"property\", \"internal\", \"hidden\", \"shortcut\", \"weak\"],\n";
        ss << "        \"string_or_number\", \"node_offset\"\n";
        ss << "      ]\n";
        ss << "    },\n";
        ss << "    \"node_count\": " << nodes_.size() << ",\n";
        ss << "    \"edge_count\": " << edges_.size() << ",\n";
        ss << "    \"trace_function_count\": 0\n";
        ss << "  },\n";

        // 2. Nodes Array: 6 integers per node
        ss << "  \"nodes\": [\n";
        for (size_t i = 0; i < nodes_.size(); ++i) {
            const auto& n = nodes_[i];
            uint32_t name_idx = get_string_id(n.name);
            ss << "    " << static_cast<int>(n.type) << ","
               << name_idx << ","
               << n.id << ","
               << n.self_size << ","
               << n.edge_count << ","
               << 0;
            if (i + 1 < nodes_.size()) ss << ",";
            ss << "\n";
        }
        ss << "  ],\n";

        // 3. Edges Array: 3 integers per edge [type, name_or_index, to_node_offset]
        ss << "  \"edges\": [\n";
        for (size_t i = 0; i < edges_.size(); ++i) {
            const auto& e = edges_[i];
            uint32_t name_or_idx = e.is_index ? e.index : get_string_id(e.name);
            uint32_t to_node_offset = static_cast<uint32_t>(e.to_node_idx * 6);
            ss << "    " << static_cast<int>(e.type) << ","
               << name_or_idx << ","
               << to_node_offset;
            if (i + 1 < edges_.size()) ss << ",";
            ss << "\n";
        }
        ss << "  ],\n";

        // 4. Strings Table
        ss << "  \"strings\": [\n";
        for (size_t i = 0; i < strings_.size(); ++i) {
            ss << "    \"";
            for (char c : strings_[i]) {
                if (c == '"') ss << "\\\"";
                else if (c == '\\') ss << "\\\\";
                else if (c == '\n') ss << "\\n";
                else if (c == '\r') ss << "\\r";
                else if (c == '\t') ss << "\\t";
                else ss << c;
            }
            ss << "\"";
            if (i + 1 < strings_.size()) ss << ",";
            ss << "\n";
        }
        ss << "  ]\n";
        ss << "}\n";

        return ss.str();
    }

    HeapSnapshotExporter::SnapshotStats stats() const {
        HeapSnapshotExporter::SnapshotStats s;
        s.node_count = nodes_.size();
        s.edge_count = edges_.size();
        s.string_count = strings_.size();
        s.total_size_bytes = 0;
        for (const auto& n : nodes_) {
            s.total_size_bytes += n.self_size;
        }
        return s;
    }
};

} // anonymous namespace

std::string HeapSnapshotExporter::export_snapshot_json(VirtualMachine& vm) {
    SnapshotBuilder builder;
    builder.build(vm);
    return builder.to_json();
}

HeapSnapshotExporter::SnapshotStats HeapSnapshotExporter::analyze_snapshot(VirtualMachine& vm) {
    SnapshotBuilder builder;
    builder.build(vm);
    return builder.stats();
}

} // namespace bvm
