// ZsValueHandle.cs — SafeHandle wrapper for ZsValue (ZsValueBox* on the C++ side).
// ReleaseHandle() calls zs_value_free() so the C++ heap object is freed when
// the C# GC collects this wrapper or when Dispose() is called explicitly.
using System;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine.Scripting;

namespace ZScript
{
    /// <summary>
    /// Owns a single ZsValue (heap-allocated on the C++ side).
    /// Dispose / using-statement is preferred; the finalizer is a safety net.
    /// </summary>
    [Preserve]
    public sealed class ZsValueHandle : SafeHandle
    {
        // SafeHandle.InvalidHandleValue = IntPtr.Zero
        public ZsValueHandle() : base(IntPtr.Zero, ownsHandle: true) { }

        /// <summary>
        /// Construct a handle wrapper around an existing native ZScript value.
        /// Intended for generated or advanced binding code.
        /// </summary>
        public ZsValueHandle(IntPtr raw, bool ownsHandle = true)
            : base(IntPtr.Zero, ownsHandle)
        {
            SetHandle(raw);
        }

        public override bool IsInvalid => handle == IntPtr.Zero;

        protected override bool ReleaseHandle()
        {
            if (handle != IntPtr.Zero)
                ZsNative.zs_value_free(handle);
            return true;
        }

        // ----------------------------------------------------------------
        // Factories
        // ----------------------------------------------------------------
        public static ZsValueHandle Nil()
            => new ZsValueHandle(ZsNative.zs_value_nil());

        public static ZsValueHandle FromBool(bool b)
            => new ZsValueHandle(ZsNative.zs_value_bool(b ? 1 : 0));

        public static ZsValueHandle FromInt(long n)
            => new ZsValueHandle(ZsNative.zs_value_int(n));

        public static ZsValueHandle FromFloat(double f)
            => new ZsValueHandle(ZsNative.zs_value_float(f));

        public static ZsValueHandle FromString(string s)
            => new ZsValueHandle(ZsNative.zs_value_string(s));

        /// <summary>Clone an existing handle — both are independently owned.</summary>
        public ZsValueHandle Clone()
            => new ZsValueHandle(ZsNative.zs_value_clone(handle));

        // ----------------------------------------------------------------
        // Accessors (safe — checks IsInvalid first)
        // ----------------------------------------------------------------
        public ZsType Type
            => IsInvalid ? ZsType.Nil : ZsNative.zs_value_type(handle);

        public bool AsBool()
            => !IsInvalid && ZsNative.zs_value_as_bool(handle) != 0;

        public long AsInt()
            => IsInvalid ? 0L : ZsNative.zs_value_as_int(handle);

        public double AsFloat()
            => IsInvalid ? 0.0 : ZsNative.zs_value_as_float(handle);

        public string AsString()
        {
            if (IsInvalid) return string.Empty;
            // zs_value_as_string returns 0 when buf==null, so use a direct buffer.
            // 4096 covers typical strings; if truncated, retry with exact size.
            byte[] buf = new byte[4096];
            int len = ZsNative.zs_value_as_string(handle, buf, buf.Length);
            if (len <= 0) return string.Empty;
            if (len >= buf.Length)
            {
                buf = new byte[len + 1];
                ZsNative.zs_value_as_string(handle, buf, buf.Length);
            }
            return Encoding.UTF8.GetString(buf, 0, Math.Min(len, buf.Length - 1));
        }

        public long AsObject()
            => IsInvalid ? -1L : ZsNative.zs_value_as_object(handle);

        /// <summary>
        /// Return the raw IntPtr for passing to native calls.
        /// The caller must NOT free this — the SafeHandle still owns it.
        /// </summary>
        public IntPtr Raw => handle;
    }
}
