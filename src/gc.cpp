#include "gc.h"
#include "chunk.h"   // Proto — for ZClosure->proto->constants

namespace zscript {

// ---------------------------------------------------------------------------
// Track / untrack
// ---------------------------------------------------------------------------
void GC::track(GcObject* obj, size_t size_estimate) {
    objects_.emplace(obj, ObjMeta{obj, Color::White, size_estimate, false});
    bytes_       += size_estimate;
    bytes_since_ += size_estimate;
}

void GC::untrack(GcObject* obj) {
    auto it = objects_.find(obj);
    if (it != objects_.end()) {
        bytes_ -= it->second.size;
        objects_.erase(it);
    }
}

// ---------------------------------------------------------------------------
// Pinning
// ---------------------------------------------------------------------------
void GC::pin(GcObject* obj) {
    auto* m = find(obj);
    if (m) m->pinned = true;
}

void GC::unpin(GcObject* obj) {
    auto* m = find(obj);
    if (m) m->pinned = false;
}

bool GC::is_pinned(GcObject* obj) const {
    auto it = objects_.find(obj);
    return (it != objects_.end()) && it->second.pinned;
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------
GC::ObjMeta* GC::find(GcObject* obj) {
    auto it = objects_.find(obj);
    return (it != objects_.end()) ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Collection trigger
// ---------------------------------------------------------------------------
bool GC::maybe_collect(std::function<void(GC&)> mark_roots) {
    if (bytes_since_ < step_alloc_limit) return false;
    collect(std::move(mark_roots));
    return true;
}

void GC::collect(std::function<void(GC&)> mark_roots) {
    // Reset all to White
    for (auto& [ptr, meta] : objects_) meta.color = Color::White;

    mark_phase(mark_roots);
    sweep_phase();

    bytes_since_ = 0;
    ++num_collections_;
}

// ---------------------------------------------------------------------------
// Mark phase
// ---------------------------------------------------------------------------
void GC::mark_phase(std::function<void(GC&)>& mark_roots) {
    // Mark all pinned objects Black immediately
    for (auto& [ptr, meta] : objects_) {
        if (meta.pinned) {
            meta.color = Color::Black;
        }
    }

    // Ask the VM to mark all roots (registers, globals, frames)
    mark_roots(*this);

    // Drain the gray worklist
    drain_gray();
}

void GC::drain_gray() {
    while (!gray_.empty()) {
        GcObject* obj = gray_.back();
        gray_.pop_back();

        auto* m = find(obj);
        if (!m || m->color == Color::Black) continue;
        m->color = Color::Black;

        // Trace children
        if (auto* t = dynamic_cast<ZTable*>(obj)) {
            mark_table(t);
        } else if (auto* c = dynamic_cast<ZClosure*>(obj)) {
            mark_closure(c);
        }
        // ZString has no children; NativeFunction has no GcObject children
    }
}

// ---------------------------------------------------------------------------
// Marking helpers (called by VM root-marking callback and by drain_gray)
// ---------------------------------------------------------------------------
void GC::mark_value(const Value& v) {
    switch (v.tag) {
        case Value::Tag::String:
            if (v.str_ptr) mark_string(v.str_ptr.get());
            break;
        case Value::Tag::Table:
            if (v.table_ptr) {
                push_gray(v.table_ptr.get());
            }
            break;
        case Value::Tag::Closure:
            if (v.closure_ptr) {
                push_gray(v.closure_ptr.get());
            }
            break;
        default:
            break;
    }
}

void GC::mark_table(ZTable* t) {
    for (auto& v : t->array)         mark_value(v);
    for (auto& [k, v] : t->hash)     mark_value(v);
}

void GC::mark_closure(ZClosure* c) {
    // Mark constant values stored in the proto's constant pool
    if (c->proto) {
        for (auto& v : c->proto->constants) mark_value(v);
    }
}

void GC::mark_string(ZString* s) {
    auto* m = find(s);
    if (m && m->color == Color::White) m->color = Color::Black;
}

void GC::push_gray(GcObject* obj) {
    auto* m = find(obj);
    if (m && m->color == Color::White) {
        m->color = Color::Gray;
        gray_.push_back(obj);
    }
}

// ---------------------------------------------------------------------------
// Sweep phase — collect all White (unreachable) objects
// ---------------------------------------------------------------------------
void GC::sweep_phase() {
    std::vector<GcObject*> dead;
    for (auto& [ptr, meta] : objects_) {
        if (meta.color == Color::White && !meta.pinned) {
            dead.push_back(ptr);
        }
    }
    // We do NOT delete the objects here because they are owned by shared_ptrs
    // in Value. We just note them as dead. In a full GC (Phase 3+) we would
    // force-clear the shared_ptr caches or use a different ownership model.
    // For now: simply untrack them so they're not counted in future cycles.
    for (auto* p : dead) {
        untrack(p);
    }
}

} // namespace zscript
