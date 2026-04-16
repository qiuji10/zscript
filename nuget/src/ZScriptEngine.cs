// ZScriptEngine.cs — Standalone .NET ZScript VM wrapper (no Unity dependency).
//
// Usage:
//   using var engine = new ZScriptEngine();
//   engine.OpenStdlib();
//   engine.LoadFile("myscript.zs");
//   using var result = engine.Call("myFunction", 42L, "hello");
//   Console.WriteLine(result?.AsString());
//
// Thread-safety: ZScriptEngine is NOT thread-safe.  Each thread should own
// its own instance.  Objects passed across threads must be synchronized
// externally.
//
using System;
using System.Text;

namespace ZScript
{
    /// <summary>
    /// Standalone .NET wrapper around a ZScript VM.  Implements
    /// <see cref="IDisposable"/>; the underlying VM is freed on Dispose.
    /// </summary>
    public sealed class ZScriptEngine : IDisposable
    {
        private IntPtr _vm;
        private bool   _disposed;

        // Keep delegate instances alive — the C++ VM holds raw function pointers.
        private ZsHandleReleaseFn? _releaseFn;

        // ------------------------------------------------------------------
        // Construction / disposal
        // ------------------------------------------------------------------
        public ZScriptEngine()
        {
            _vm = ZsNative.zs_vm_new();
            if (_vm == IntPtr.Zero)
                throw new InvalidOperationException("zs_vm_new() returned null.");
        }

        public void Dispose()
        {
            if (!_disposed)
            {
                _disposed = true;
                ZsNative.zs_vm_free(_vm);
                _vm = IntPtr.Zero;
            }
        }

        // ------------------------------------------------------------------
        // VM setup
        // ------------------------------------------------------------------
        /// <summary>Open the ZScript standard library (math, string, io, coroutine, …).</summary>
        public void OpenStdlib()
        {
            ThrowIfDisposed();
            ZsNative.zs_vm_open_stdlib(_vm);
        }

        /// <summary>Register a callback invoked when an object handle is GC'd.</summary>
        public void SetHandleReleaseCallback(ZsHandleReleaseFn fn)
        {
            ThrowIfDisposed();
            _releaseFn = fn;
            ZsNative.zs_vm_set_handle_release(_vm, fn);
        }

        // ------------------------------------------------------------------
        // Tag system
        // ------------------------------------------------------------------
        public void   AddTag(string tag)    { ThrowIfDisposed(); ZsNative.zs_vm_add_tag(_vm, tag); }
        public void   RemoveTag(string tag) { ThrowIfDisposed(); ZsNative.zs_vm_remove_tag(_vm, tag); }
        public bool   HasTag(string tag)    { ThrowIfDisposed(); return ZsNative.zs_vm_has_tag(_vm, tag) != 0; }

        // ------------------------------------------------------------------
        // Script loading
        // ------------------------------------------------------------------
        /// <summary>Load and execute a script file.  Throws <see cref="ZScriptException"/> on error.</summary>
        public void LoadFile(string path)
        {
            ThrowIfDisposed();
            byte[] err = new byte[1024];
            if (ZsNative.zs_vm_load_file(_vm, path, err, err.Length) == 0)
                throw new ZScriptException(DecodeErr(err));
        }

        /// <summary>Load and execute source code.  Throws <see cref="ZScriptException"/> on error.</summary>
        public void LoadSource(string name, string source)
        {
            ThrowIfDisposed();
            byte[] err = new byte[1024];
            if (ZsNative.zs_vm_load_source(_vm, name, source, err, err.Length) == 0)
                throw new ZScriptException(DecodeErr(err));
        }

        // ------------------------------------------------------------------
        // Function calls
        // ------------------------------------------------------------------
        /// <summary>
        /// Call a named ZScript global function.
        /// Arguments are automatically converted: <c>bool</c>, <c>long</c>,
        /// <c>int</c>, <c>double</c>, <c>float</c>, <c>string</c>,
        /// and <see cref="ZScriptValue"/> are all accepted.
        /// Returns the first return value, or null if the function returns nil.
        /// Throws <see cref="ZScriptException"/> on runtime error.
        /// </summary>
        public ZScriptValue? Call(string name, params object?[] args)
        {
            ThrowIfDisposed();
            IntPtr[] rawArgs = MarshalArgs(args);
            byte[] err = new byte[1024];
            int ok = ZsNative.zs_vm_call(_vm, name, rawArgs.Length, rawArgs,
                                          out IntPtr outRaw, err, err.Length);
            FreeArgs(rawArgs, args);
            if (ok == 0) throw new ZScriptException(DecodeErr(err));
            var v = new ZScriptValue(outRaw);
            return v.IsNil ? null : v;
        }

        // ------------------------------------------------------------------
        // Native function registration
        // ------------------------------------------------------------------
        public void RegisterFunction(string name, ZsNativeFn fn)
        {
            ThrowIfDisposed();
            ZsNative.zs_vm_register_fn(_vm, name, fn);
        }

        // ------------------------------------------------------------------
        // Global access
        // ------------------------------------------------------------------
        public ZScriptValue? GetGlobal(string name)
        {
            ThrowIfDisposed();
            var v = new ZScriptValue(ZsNative.zs_vm_get_global(_vm, name));
            return v.IsNil ? null : v;
        }

        public void SetGlobal(string name, ZScriptValue value)
        {
            ThrowIfDisposed();
            ZsNative.zs_vm_set_global(_vm, name, value.Raw);
        }

        // ------------------------------------------------------------------
        // Annotation queries
        // ------------------------------------------------------------------
        public string[] GetClassAnnotations(string className)
        {
            ThrowIfDisposed();
            byte[] buf = new byte[1024];
            int count = ZsNative.zs_vm_get_class_annotations(_vm, className, buf, buf.Length);
            return count > 0 ? ParseNulSeparated(buf, count) : [];
        }

        public string[] FindAnnotatedClasses(string ns, string annotationName)
        {
            ThrowIfDisposed();
            byte[] buf = new byte[4096];
            int count = ZsNative.zs_vm_find_annotated_classes(_vm, ns, annotationName, buf, buf.Length);
            return count > 0 ? ParseNulSeparated(buf, count) : [];
        }

        // ------------------------------------------------------------------
        // Hotpatch
        // ------------------------------------------------------------------
        /// <summary>
        /// Enable live-reload from the given directory.
        /// Call <see cref="Poll"/> each tick to apply reloads.
        /// </summary>
        public bool EnableHotpatch(string dir)
        {
            ThrowIfDisposed();
            return ZsNative.zs_vm_enable_hotpatch(_vm, dir) != 0;
        }

        /// <summary>
        /// Process pending hotpatch reloads.
        /// Returns the number of scripts reloaded.  Call each tick / frame.
        /// </summary>
        public int Poll()
        {
            ThrowIfDisposed();
            return ZsNative.zs_vm_poll(_vm);
        }

        // ------------------------------------------------------------------
        // Raw handle (for interop with ZsNativeFn callbacks)
        // ------------------------------------------------------------------
        public IntPtr RawVM => _vm;

        // ------------------------------------------------------------------
        // Helpers
        // ------------------------------------------------------------------
        private static string DecodeErr(byte[] buf)
            => Encoding.UTF8.GetString(buf).TrimEnd('\0');

        private void ThrowIfDisposed()
        {
            if (_disposed) throw new ObjectDisposedException(nameof(ZScriptEngine));
        }

        private static IntPtr[] MarshalArgs(object?[] args)
        {
            var raw = new IntPtr[args.Length];
            for (int i = 0; i < args.Length; ++i)
            {
                raw[i] = args[i] switch
                {
                    null             => ZsNative.zs_value_nil(),
                    bool b           => ZsNative.zs_value_bool(b ? 1 : 0),
                    long l           => ZsNative.zs_value_int(l),
                    int n            => ZsNative.zs_value_int(n),
                    double d         => ZsNative.zs_value_float(d),
                    float f          => ZsNative.zs_value_float(f),
                    string s         => ZsNative.zs_value_string(s),
                    ZScriptValue v   => ZsNative.zs_value_clone(v.Raw),
                    _                => ZsNative.zs_value_string(args[i]!.ToString() ?? "")
                };
            }
            return raw;
        }

        private static void FreeArgs(IntPtr[] raw, object?[] args)
        {
            // Free only args we allocated; ZScriptValue args were cloned.
            for (int i = 0; i < raw.Length; ++i)
            {
                // Always free — we either allocated a fresh box or cloned.
                ZsNative.zs_value_free(raw[i]);
            }
        }

        private static string[] ParseNulSeparated(byte[] buf, int maxCount)
        {
            var results = new System.Collections.Generic.List<string>(maxCount);
            int start = 0;
            for (int i = 0; i <= buf.Length && results.Count < maxCount; ++i)
            {
                if (i == buf.Length || buf[i] == 0)
                {
                    if (i > start)
                        results.Add(Encoding.UTF8.GetString(buf, start, i - start));
                    start = i + 1;
                    if (i < buf.Length && buf[i] == 0 && (i + 1 >= buf.Length || buf[i + 1] == 0))
                        break;
                }
            }
            return [.. results];
        }
    }

    // ------------------------------------------------------------------
    // Exception type
    // ------------------------------------------------------------------
    /// <summary>Thrown when a ZScript operation fails (compile or runtime error).</summary>
    public sealed class ZScriptException : Exception
    {
        public ZScriptException(string message) : base(message) { }
        public ZScriptException(string message, Exception inner) : base(message, inner) { }
    }
}
