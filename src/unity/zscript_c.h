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
ZsVM  zs_vm_new(void);
void  zs_vm_free(ZsVM vm);
void  zs_vm_open_stdlib(ZsVM vm);

// ---------------------------------------------------------------------------
// Tag system
// ---------------------------------------------------------------------------
int   zs_vm_add_tag(ZsVM vm, const char* tag);
void  zs_vm_remove_tag(ZsVM vm, const char* tag);
int   zs_vm_has_tag(ZsVM vm, const char* tag);

// ---------------------------------------------------------------------------
// Script execution
// ---------------------------------------------------------------------------
// Returns 1 on success, 0 on error (writes message into err_buf).
int   zs_vm_load_file(ZsVM vm, const char* path,
                      char* err_buf, int err_len);
int   zs_vm_load_source(ZsVM vm, const char* name, const char* source,
                        char* err_buf, int err_len);

// ---------------------------------------------------------------------------
// Function calls
// ---------------------------------------------------------------------------
// Call a named global function. out_result receives the first return value
// (caller must zs_value_free it). Returns 1 on success, 0 on error.
int   zs_vm_call(ZsVM vm, const char* name,
                 int argc, ZsValue* argv,
                 ZsValue* out_result,
                 char* err_buf, int err_len);

// ---------------------------------------------------------------------------
// Native function registration
// ---------------------------------------------------------------------------
void  zs_vm_register_fn(ZsVM vm, const char* name, ZsNativeFn fn);

// ---------------------------------------------------------------------------
// Object handle system
// ---------------------------------------------------------------------------
// Register the callback invoked when a proxy table is GC'd.
void   zs_vm_set_handle_release(ZsVM vm, ZsHandleReleaseFn fn);
// Create a ZScript proxy table for the given handle id.
// Returns a new ZsValue (caller must free it).
ZsValue zs_vm_push_object_handle(ZsVM vm, int64_t handle_id);
// Extract the handle id from a ZsValue. Returns -1 if not a handle.
int64_t zs_vm_get_object_handle(ZsVM vm, ZsValue val);

// Set / get a named field on a handle's proxy table (used by binding gen).
void    zs_vm_handle_set_field(ZsVM vm, ZsValue handle_val,
                               const char* name, ZsValue field_val);
ZsValue zs_vm_handle_get_field(ZsVM vm, ZsValue handle_val, const char* name);

// Register __index / __newindex / __call on a handle's proxy table.
// These are set per-type by the binding layer after creating the prototype table.
void    zs_vm_handle_set_index(ZsVM vm, ZsValue handle_val, ZsNativeFn fn);
void    zs_vm_handle_set_newindex(ZsVM vm, ZsValue handle_val, ZsNativeFn fn);
void    zs_vm_handle_set_call(ZsVM vm, ZsValue handle_val, ZsNativeFn fn);

// ---------------------------------------------------------------------------
// Hotpatch
// ---------------------------------------------------------------------------
int   zs_vm_enable_hotpatch(ZsVM vm, const char* dir);
int   zs_vm_poll(ZsVM vm); // call each frame from Update(); returns reload count

// ---------------------------------------------------------------------------
// Error query
// ---------------------------------------------------------------------------
// Copies the last error message into buf (NUL-terminated, truncated to fit).
void  zs_vm_last_error(ZsVM vm, char* buf, int len);

// ---------------------------------------------------------------------------
// Value constructors  (each returns a new heap-allocated ZsValue)
// ---------------------------------------------------------------------------
ZsValue zs_value_nil(void);
ZsValue zs_value_bool(int b);
ZsValue zs_value_int(int64_t n);
ZsValue zs_value_float(double f);
ZsValue zs_value_string(const char* s);
// Wraps an existing object handle (same as zs_vm_push_object_handle but takes
// an already-allocated proxy Value — used when the proxy was set up externally).
ZsValue zs_value_object(ZsVM vm, int64_t handle_id);

// ---------------------------------------------------------------------------
// Value accessors
// ---------------------------------------------------------------------------
ZsType  zs_value_type(ZsValue val);
int     zs_value_as_bool(ZsValue val);
int64_t zs_value_as_int(ZsValue val);
double  zs_value_as_float(ZsValue val);
// Copies the string into buf (NUL-terminated, truncated). Returns byte length.
int     zs_value_as_string(ZsValue val, char* buf, int len);
int64_t zs_value_as_object(ZsValue val); // returns handle id or -1

// ---------------------------------------------------------------------------
// Value lifetime
// ---------------------------------------------------------------------------
void    zs_value_free(ZsValue val);
ZsValue zs_value_clone(ZsValue val);

#ifdef __cplusplus
} // extern "C"
#endif
