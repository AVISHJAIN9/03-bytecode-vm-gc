#ifndef BYTECODE_VM_WRITE_BARRIER_HPP
#define BYTECODE_VM_WRITE_BARRIER_HPP

#include "gc_types.hpp"
#include "value.hpp"
#include "card_table.hpp"
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <functional>

namespace bvm {

/// Barrier mode selection
enum class BarrierMode : uint8_t {
    DIJKSTRA = 0, // Shading newly referenced White target to Gray
    STEELE   = 1  // Shading/demoting Black source container to Gray
};

/// High-Performance Mutator Write Barrier (Features 21 & 22)
/// Preserves tri-color invariants during incremental/concurrent marking
class WriteBarrier {
public:
    /// Dijkstra-style write barrier:
    /// If container is Black and new field points to a White object, shade target to Gray and enqueue.
    static inline void dijkstra_barrier(
        GCPhase current_phase,
        GrayWorklist& worklist,
        ObjHeader* container,
        ObjHeader* target,
        size_t* barrier_counter = nullptr
    ) {
        if (current_phase != GCPhase::MARKING) return;
        if (!container || !target) return;

        // If a Black container is made to point to a White target, shade target Gray (strong invariant)
        if (container->is_black() && target->is_white()) {
            target->mark_gray();
            worklist.push(target);
            if (barrier_counter) {
                (*barrier_counter)++;
            }
        }
    }

    /// Steele-style write barrier:
    /// If container is Black, demote container back to Gray and re-enqueue to rescan children.
    static inline void steele_barrier(
        GCPhase current_phase,
        GrayWorklist& worklist,
        ObjHeader* container,
        size_t* barrier_counter = nullptr
    ) {
        if (current_phase != GCPhase::MARKING) return;
        if (!container) return;

        if (container->is_black()) {
            container->mark_gray();
            worklist.push(container);
            if (barrier_counter) {
                (*barrier_counter)++;
            }
        }
    }

    /// Combined write barrier on arbitrary Value mutation:
    /// Extracts ObjHeader if Value is an OBJECT and applies the barrier.
    static inline void on_field_write(
        GCPhase current_phase,
        GrayWorklist& worklist,
        ObjHeader* container,
        const Value& new_val,
        size_t* barrier_counter = nullptr,
        BarrierMode mode = BarrierMode::DIJKSTRA
    ) {
        if (current_phase != GCPhase::MARKING) return;
        if (!container || !new_val.is_object()) return;

        ObjHeader* target = new_val.as_object();
        if (!target) return;

        if (mode == BarrierMode::DIJKSTRA) {
            dijkstra_barrier(current_phase, worklist, container, target, barrier_counter);
        } else {
            steele_barrier(current_phase, worklist, container, barrier_counter);
        }
    }

    /// Tri-color invariant verification:
    /// Asserts that no Black object directly references a White object.
    static bool verify_tri_color_invariant(ObjHeader* obj) {
        if (!obj || !obj->is_black()) return true;

        bool invariant_holds = true;
        visit_children(obj, [&](ObjHeader* child) {
            if (child && child->is_white()) {
                invariant_holds = false; // Tri-color invariant violation!
            }
        });
        return invariant_holds;
    }

    /// Generational Card Table Post-Write Barrier (Feature 25):
    /// When an Old object field is mutated to reference a Young object, marks the corresponding card dirty.
    static inline void card_table_post_barrier(
        CardTable* card_table,
        ObjHeader* container,
        const Value& new_val
    ) {
        if (!card_table || !container) return;
        if (container->is_old_gen() && new_val.is_object() && new_val.as_object() != nullptr) {
            ObjHeader* target = new_val.as_object();
            if (!target->is_old_gen()) {
                card_table->mark_dirty(container);
            }
        }
    }
};

} // namespace bvm

#endif // BYTECODE_VM_WRITE_BARRIER_HPP
