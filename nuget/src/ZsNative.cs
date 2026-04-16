// ZsNative.cs — P/Invoke declarations for the ZScript C API (standalone .NET).
// Mirrors zscript_c.h exactly.  Library name: "zscript" (resolved at runtime
// by the NativeLibrary loader and the platform-specific runtimes/ folder).
using System;
using System.Runtime.InteropServices;

namespace ZScript
{
    // -----------------------------------------------------------------------
    // Value type tag
    // -----------------------------------------------------------------------
    public enum ZsType : int
    {
        Nil       = 0,
        Bool      = 1,
        Int       = 2,
        Float     = 3,
        String    = 4,
        Table     = 5,
        Closure   = 6,
        Native    = 7,
        Delegate  = 8,
        Coroutine = 9,
        Object    = 100,
    }

    // -----------------------------------------------------------------------
    // Delegate types
    // -----------------------------------------------------------------------
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate IntPtr ZsNativeFn(IntPtr vm, int argc, IntPtr[] argv);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void ZsHandleReleaseFn(IntPtr vm, long handleId);

    // -----------------------------------------------------------------------
    // Raw P/Invoke layer — lib name "zscript" (no Unity suffix).
    // -----------------------------------------------------------------------
    internal static class ZsNative
    {
        private const string Lib = "zscript";

        // VM lifecycle
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zs_vm_new();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zs_vm_free(IntPtr vm);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zs_vm_open_stdlib(IntPtr vm);

        // Tag system
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_vm_add_tag(IntPtr vm, string tag);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zs_vm_remove_tag(IntPtr vm, string tag);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_vm_has_tag(IntPtr vm, string tag);

        // Script execution
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_vm_load_file(
            IntPtr vm, string path, [Out] byte[] errBuf, int errLen);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_vm_load_source(
            IntPtr vm, string name, string source, [Out] byte[] errBuf, int errLen);

        // Function calls
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_vm_call(
            IntPtr vm, string name, int argc, IntPtr[] argv,
            out IntPtr outResult, [Out] byte[] errBuf, int errLen);

        // Native function registration
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zs_vm_register_fn(IntPtr vm, string name, ZsNativeFn fn);

        // Object handle system
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zs_vm_set_handle_release(IntPtr vm, ZsHandleReleaseFn fn);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zs_vm_push_object_handle(IntPtr vm, long handleId);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern long zs_vm_get_object_handle(IntPtr vm, IntPtr val);

        // Hotpatch
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_vm_enable_hotpatch(IntPtr vm, string dir);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_vm_poll(IntPtr vm);

        // Error query
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zs_vm_last_error(IntPtr vm, [Out] byte[] buf, int len);

        // Global access
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zs_vm_get_global(IntPtr vm, string name);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zs_vm_set_global(IntPtr vm, string name, IntPtr val);

        // Value constructors
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zs_value_nil();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zs_value_bool(int b);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zs_value_int(long n);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zs_value_float(double f);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zs_value_string(string s);

        // Value accessors
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern ZsType zs_value_type(IntPtr val);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_value_as_bool(IntPtr val);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern long zs_value_as_int(IntPtr val);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern double zs_value_as_float(IntPtr val);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_value_as_string(IntPtr val, [Out] byte[] buf, int len);

        // Value lifetime
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void zs_value_free(IntPtr val);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zs_value_clone(IntPtr val);

        // Value utilities
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern long zs_value_identity(IntPtr val);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_value_invoke(
            IntPtr vm, IntPtr fn, int argc, IntPtr[] argv,
            out IntPtr outResult, [Out] byte[] errBuf, int errLen);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_vm_invoke_method(
            IntPtr vm, IntPtr obj, string method, int argc, IntPtr[] argv,
            out IntPtr outResult, [Out] byte[] errBuf, int errLen);

        // Annotation query
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_vm_get_class_annotations(
            IntPtr vm, string className, [Out] byte[] buf, int bufLen);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_vm_find_annotated_classes(
            IntPtr vm, string ns, string name, [Out] byte[] buf, int bufLen);

        // Coroutine API
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr zs_coroutine_create(IntPtr vm, IntPtr fnVal);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_coroutine_resume(
            IntPtr vm, IntPtr coVal, int argc, IntPtr[] argv, out IntPtr outValue);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int zs_coroutine_status(IntPtr vm, IntPtr coVal);
    }
}
