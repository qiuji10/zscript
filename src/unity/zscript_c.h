// zscript_c.h — ZScript C API for Unity (P/Invoke safe)
//
// All functions use C linkage and simple value types so they cross the
// C#/C++ boundary via P/Invoke without marshalling overhead.
//
// Ownership rules:
//   ZsValue  — heap-allocated wrapper; caller must call zs_value_free() when done.
//              Clone with zs_value_clone() if the value must outlive the call site.
//   ZsVM     — opaque pointer to VM; created by zs_vm_new(), freed by zs_vm_free().
//
// Error handling:
//   Functions that can fail return 0 (false) on error and write a message into
//   the provided err_buf / err_len pair (NUL-terminated, truncated to fit).
//   Retrieve the last error string any time with zs_vm_last_error().
//
#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define ZS_API __declspec(dllexport)
#else
#  define ZS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Opaque handle types
// ---------------------------------------------------------------------------
typedef void* ZsVM;    // zscript::VM*
typedef void* ZsValue; // ZsValueBox* (heap-allocated Value wrapper)

// ---------------------------------------------------------------------------
// Value type tag (mirrors zscript::Value::Tag)
// ---------------------------------------------------------------------------
typedef enum {
    ZS_TYPE_NIL      = 0,
    ZS_TYPE_BOOL     = 1,
    ZS_TYPE_INT      = 2,
    ZS_TYPE_FLOAT    = 3,
    ZS_TYPE_STRING   = 4,
    ZS_TYPE_TABLE    = 5,
    ZS_TYPE_CLOSURE  = 6,
    ZS_TYPE_NATIVE   = 7,
    ZS_TYPE_DELEGATE = 8,
    ZS_TYPE_COROUTINE= 9,
    ZS_TYPE_OBJECT   = 100, // object handle (ZTable with __handle)
} ZsType;

// ---------------------------------------------------------------------------
// Native function signature
// argc / argv do NOT include the ZsVM pointer — the callback receives that
// separately so it can call back into the VM if needed.
// ---------------------------------------------------------------------------
typedef ZsValue (*ZsNativeFn)(ZsVM vm, int argc, ZsValue* argv);

// ---------------------------------------------------------------------------
// Handle release callback — fired when a proxy table is GC'd.
// Registered once via zs_vm_set_handle_release().
// ---------------------------------------------------------------------------
typedef void (*ZsHandleReleaseFn)(ZsVM vm, int64_t handle_id);

// ---------------------------------------------------------------------------
// VM lifecycle
// ---------------------------------------------------------------------------
ZS_API ZsVM  zs_vm_new(void);
ZS_API void  zs_vm_free(ZsVM vm);
ZS_API void  zs_vm_open_stdlib(ZsVM vm);

// ---------------------------------------------------------------------------
// Tag system
// ---------------------------------------------------------------------------
ZS_API int   zs_vm_add_tag(ZsVM vm, const char* tag);
ZS_API void  zs_vm_remove_tag(ZsVM vm, const char* tag);
ZS_API int   zs_vm_has_tag(ZsVM vm, const char* tag);

// ---------------------------------------------------------------------------
// Script execution
// ---------------------------------------------------------------------------
// Returns 1 on success, 0 on error (writes message into err_buf).
ZS_API int   zs_vm_load_file(ZsVM vm, const char* path,
                             char* err_buf, int err_len);
ZS_API int   zs_vm_load_source(ZsVM vm, const char* name, const char* source,
                               char* err_buf, int err_len);

// ---------------------------------------------------------------------------
// Function calls
// ---------------------------------------------------------------------------
// Call a named global function. out_result receives the first return value
// (caller must zs_value_free it). Returns 1 on success, 0 on error.
ZS_API int   zs_vm_call(ZsVM vm, const char* name,
                        int argc, ZsValue* argv,
                        ZsValue* out_result,
                        char* err_buf, int err_len);

// ---------------------------------------------------------------------------
// Native function registration
// ---------------------------------------------------------------------------
ZS_API void  zs_vm_register_fn(ZsVM vm, const char* name, ZsNativeFn fn);

// ---------------------------------------------------------------------------
// Global value access
// ---------------------------------------------------------------------------
// Returns a new ZsValue for the named global (caller must free).
// Returns a nil ZsValue if the global does not exist.
ZS_API ZsValue zs_vm_get_global(ZsVM vm, const char* name);
// Set a named global to val (copies the inner value; caller retains ownership of val).
ZS_API void    zs_vm_set_global(ZsVM vm, const char* name, ZsValue val);

// ---------------------------------------------------------------------------
// Object handle system
// ---------------------------------------------------------------------------
// Register the callback invoked when a proxy table is GC'd.
ZS_API void   zs_vm_set_handle_release(ZsVM vm, ZsHandleReleaseFn fn);
// Create a ZScript proxy table for the given handle id.
// Returns a new ZsValue (caller must free it).
ZS_API ZsValue zs_vm_push_object_handle(ZsVM vm, int64_t handle_id);
// Extract the handle id from a ZsValue. Returns -1 if not a handle.
ZS_API int64_t zs_vm_get_object_handle(ZsVM vm, ZsValue val);

// Set / get a named field on a handle's proxy table (used by binding gen).
ZS_API void    zs_vm_handle_set_field(ZsVM vm, ZsValue handle_val,
                                      const char* name, ZsValue field_val);
ZS_API ZsValue zs_vm_handle_get_field(ZsVM vm, ZsValue handle_val, const char* name);

// Register __index / __newindex / __call on a handle's proxy table.
// These are set per-type by the binding layer after creating the prototype table.
ZS_API void    zs_vm_handle_set_index(ZsVM vm, ZsValue handle_val, ZsNativeFn fn);
ZS_API void    zs_vm_handle_set_newindex(ZsVM vm, ZsValue handle_val, ZsNativeFn fn);
ZS_API void    zs_vm_handle_set_call(ZsVM vm, ZsValue handle_val, ZsNativeFn fn);

// ---------------------------------------------------------------------------
// Hotpatch
// ---------------------------------------------------------------------------
ZS_API int   zs_vm_enable_hotpatch(ZsVM vm, const char* dir);
ZS_API int   zs_vm_poll(ZsVM vm); // call each frame from Update(); returns reload count

// ---------------------------------------------------------------------------
// Error query
// ---------------------------------------------------------------------------
// Copies the last error message into buf (NUL-terminated, truncated to fit).
ZS_API void  zs_vm_last_error(ZsVM vm, char* buf, int len);

// ---------------------------------------------------------------------------
// Value constructors  (each returns a new heap-allocated ZsValue)
// ---------------------------------------------------------------------------
ZS_API ZsValue zs_value_nil(void);
ZS_API ZsValue zs_value_bool(int b);
ZS_API ZsValue zs_value_int(int64_t n);
ZS_API ZsValue zs_value_float(double f);
ZS_API ZsValue zs_value_string(const char* s);
// Wraps an existing object handle (same as zs_vm_push_object_handle but takes
// an already-allocated proxy Value — used when the proxy was set up externally).
ZS_API ZsValue zs_value_object(ZsVM vm, int64_t handle_id);

// ---------------------------------------------------------------------------
// Value accessors
// ---------------------------------------------------------------------------
ZS_API ZsType  zs_value_type(ZsValue val);
ZS_API int     zs_value_as_bool(ZsValue val);
ZS_API int64_t zs_value_as_int(ZsValue val);
ZS_API double  zs_value_as_float(ZsValue val);
// Copies the string into buf (NUL-terminated, truncated). Returns byte length.
ZS_API int     zs_value_as_string(ZsValue val, char* buf, int len);
ZS_API int64_t zs_value_as_object(ZsValue val); // returns handle id or -1

// ---------------------------------------------------------------------------
// Value lifetime
// ---------------------------------------------------------------------------
ZS_API void    zs_value_free(ZsValue val);
ZS_API ZsValue zs_value_clone(ZsValue val);

// ---------------------------------------------------------------------------
// Value utilities
// ---------------------------------------------------------------------------
// Returns a stable integer that identifies the inner heap object of a value
// (the shared_ptr raw pointer cast to int64). Suitable for use as a map key
// to detect "same closure / same table" identity. Returns 0 for value types
// (nil, bool, int, float, string).
ZS_API int64_t zs_value_identity(ZsValue val);

// Call any callable ZsValue (closure or native) with the given args.
// out_result receives the first return value (caller must free).
// Returns 1 on success, 0 on error (writes message into err_buf).
ZS_API int zs_value_invoke(ZsVM vm, ZsValue fn,
                           int argc, ZsValue* argv,
                           ZsValue* out_result,
                           char* err_buf, int err_len);

// Call a named method on a ZScript table value (looks up key, then calls with
// obj as implicit first argument). Returns 1 on success, 0 on error.
ZS_API int zs_vm_invoke_method(ZsVM vm, ZsValue obj,
                               const char* method,
                               int argc, ZsValue* argv,
                               ZsValue* out_result,
                               char* err_buf, int err_len);

// ---------------------------------------------------------------------------
// Table construction  (for building ZScript values from C#)
// ---------------------------------------------------------------------------
// Create a new empty ZScript table (plain table, not an object proxy).
ZS_API ZsValue zs_table_new(void);
// Set a string-keyed field on a table to an arbitrary value.
ZS_API void    zs_table_set_value(ZsValue tbl, const char* key, ZsValue val);
// Set a string-keyed field to a native C function (wraps fn in from_native).
ZS_API void    zs_table_set_fn(ZsValue tbl, const char* key, ZsNativeFn fn, ZsVM vm);

// ---------------------------------------------------------------------------
// Coroutine API
// ---------------------------------------------------------------------------
// Create a coroutine from a ZScript closure value.
// Returns a new ZsValue of type ZS_TYPE_COROUTINE (caller must free it).
ZS_API ZsValue zs_coroutine_create(ZsVM vm, ZsValue fn_val);

// Resume a coroutine with the given arguments.
//   out_value — receives the first value yielded/returned (caller must free).
// Returns:
//   1  — coroutine yielded; it is still suspended; out_value is the yield arg.
//   0  — coroutine finished (returned normally or errored); out_value is the
//         return value or an error string.
//  -1  — val is not a coroutine.
ZS_API int zs_coroutine_resume(ZsVM vm, ZsValue co_val,
                               int argc, ZsValue* argv,
                               ZsValue* out_value);

// Return the current status of a coroutine.
//   0 = suspended, 1 = running, 2 = dead, -1 = not a coroutine
ZS_API int zs_coroutine_status(ZsVM vm, ZsValue co_val);

// ---------------------------------------------------------------------------
// Annotation query API
// ---------------------------------------------------------------------------
// Returns the number of annotations on class_name, or 0 if the class has none.
// If buf / buf_len are non-null/non-zero, writes a NUL-separated list of
// "ns.name" strings (e.g. "unity.component\0unity.serialize\0") into buf.
ZS_API int zs_vm_get_class_annotations(ZsVM vm, const char* class_name,
                                       char* buf, int buf_len);

// Find all class names that carry the annotation ns.name
// (e.g. ns="unity", name="component").
// Writes a NUL-separated list of class names into buf.
// Returns the number of matching classes found.
ZS_API int zs_vm_find_annotated_classes(ZsVM vm, const char* ns, const char* name,
                                        char* buf, int buf_len);

#ifdef __cplusplus
} // extern "C"
#endif
