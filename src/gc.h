#pragma once
#include "value.h"
#include <cstddef>
#include <functional>
#include <unordered_set>
#include <vector>

namespace zscript {

// ===========================================================================
// Tri-color incremental mark-and-sweep GC
//
// All GcObject subclasses (ZString, ZTable, ZClosure, NativeFunction) that
// are allocated through the GC are tracked here.  The VM registers its roots
// before each collection.
//
// Color encoding:
//   White  — not yet reached; will be freed if still white after mark phase
//   Gray   — discovered but children not yet scanned
//   Black  — fully scanned
//
// Phase 2 used shared_ptr for convenience.  The GC layer co-exists with that:
// GcObjects are still shared_ptr-managed so the C++ side can hold references
// safely, but the GC additionally tracks them and can mark objects unreachable
// even if no ZScript Value references remain but a cycle keeps a shared_ptr
// alive (full cycle-breaking is Phase 3 stretch goal).
// ===========================================================================

class GC {
public:
    // ---- configuration ----
    size_t step_alloc_limit = 1024 * 64; // collect after this many bytes allocated

    // ---- lifecycle ----
    GC() = default;
    ~GC() = default;

    // ---- object registration ----
    // Called by VM whenever a new heap object is created.
    void track(GcObject* obj, size_t size_estimate = 64);
    // Remove an object from tracking (called from destructor via weak callback).
    void untrack(GcObject* obj);

    // ---- pinning (for C++ side) ----
    void pin(GcObject* obj);
    void unpin(GcObject* obj);
    bool is_pinned(GcObject* obj) const;

    // ---- collection trigger ----
    // Returns true if a collection was performed.
    bool maybe_collect(std::function<void(GC&)> mark_roots);
    void collect(std::function<void(GC&)> mark_roots);

    // ---- marking (called by roots / object tracers) ----
    void mark_value(const Value& v);
    void mark_table(ZTable* t);
    void mark_closure(ZClosure* c);
    void mark_string(ZString* s);

    // ---- stats ----
    size_t bytes_allocated() const { return bytes_; }
    size_t num_objects()     const { return objects_.size(); }
    size_t num_collections() const { return num_collections_; }

private:
    enum class Color : uint8_t { White, Gray, Black };

    struct ObjMeta {
        GcObject* ptr;
        Color     color  = Color::White;
        size_t    size   = 64;
        bool      pinned = false;
    };

    // Find metadata for a given pointer (O(1) via hash)
    ObjMeta* find(GcObject* obj);

    void mark_phase(std::function<void(GC&)>& mark_roots);
    void sweep_phase();

    // Gray worklist
    void push_gray(GcObject* obj);
    void drain_gray();

    std::unordered_map<GcObject*, ObjMeta> objects_;
    std::vector<GcObject*>                 gray_;
    size_t bytes_       = 0;
    size_t bytes_since_ = 0;  // bytes allocated since last collection
    size_t num_collections_ = 0;
};

} // namespace zscript
