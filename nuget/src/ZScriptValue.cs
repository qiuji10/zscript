// ZScriptValue.cs — Strongly-typed ZScript value for the standalone .NET package.
//
// Wraps a raw ZsValue* (via ZsValueHandle) and exposes implicit conversions to
// and from the common .NET primitive types.  Values are immutable snapshots;
// call Dispose() (or use `using`) to release the C++ heap allocation.
//
// Implicit conversion rules mirror Lua-like coercion:
//   bool   ← ZsType.Bool (0 = false, anything else = true)
//   long   ← ZsType.Int
//   double ← ZsType.Float  (also accepts Int via widening)
//   string ← ZsType.String (also accepts any type via ToString())
//
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace ZScript
{
    /// <summary>
    /// A ZScript value returned by the VM.  Dispose when done.
    /// </summary>
    public sealed class ZScriptValue : IDisposable
    {
        private readonly IntPtr _raw;
        private bool _disposed;

        // ------------------------------------------------------------------
        // Construction
        // ------------------------------------------------------------------
        internal ZScriptValue(IntPtr raw) => _raw = raw;

        public static ZScriptValue Nil    => new(ZsNative.zs_value_nil());
        public static ZScriptValue True   => new(ZsNative.zs_value_bool(1));
        public static ZScriptValue False  => new(ZsNative.zs_value_bool(0));

        public static implicit operator ZScriptValue(bool v)   => new(ZsNative.zs_value_bool(v ? 1 : 0));
        public static implicit operator ZScriptValue(long v)   => new(ZsNative.zs_value_int(v));
        public static implicit operator ZScriptValue(int v)    => new(ZsNative.zs_value_int(v));
        public static implicit operator ZScriptValue(double v) => new(ZsNative.zs_value_float(v));
        public static implicit operator ZScriptValue(float v)  => new(ZsNative.zs_value_float(v));
        public static implicit operator ZScriptValue(string v) => new(ZsNative.zs_value_string(v ?? ""));

        // ------------------------------------------------------------------
        // Type and accessors
        // ------------------------------------------------------------------
        public ZsType Type => _disposed ? ZsType.Nil : ZsNative.zs_value_type(_raw);

        public bool   IsNil    => Type == ZsType.Nil;
        public bool   IsBool   => Type == ZsType.Bool;
        public bool   IsInt    => Type == ZsType.Int;
        public bool   IsFloat  => Type == ZsType.Float;
        public bool   IsString => Type == ZsType.String;
        public bool   IsTable  => Type == ZsType.Table;

        public bool AsBool()
        {
            if (_disposed) return false;
            return ZsNative.zs_value_as_bool(_raw) != 0;
        }

        public long AsLong()
        {
            if (_disposed) return 0;
            return Type == ZsType.Float
                ? (long)ZsNative.zs_value_as_float(_raw)
                : ZsNative.zs_value_as_int(_raw);
        }

        public double AsDouble()
        {
            if (_disposed) return 0.0;
            return Type == ZsType.Int
                ? (double)ZsNative.zs_value_as_int(_raw)
                : ZsNative.zs_value_as_float(_raw);
        }

        public string AsString()
        {
            if (_disposed) return "";
            int len = ZsNative.zs_value_as_string(_raw, null!, 0);
            byte[] buf = new byte[len + 1];
            ZsNative.zs_value_as_string(_raw, buf, buf.Length);
            return Encoding.UTF8.GetString(buf, 0, len);
        }

        // ------------------------------------------------------------------
        // Implicit conversions from ZScriptValue
        // ------------------------------------------------------------------
        public static implicit operator bool(ZScriptValue v)   => v?.AsBool()   ?? false;
        public static implicit operator long(ZScriptValue v)   => v?.AsLong()   ?? 0L;
        public static implicit operator double(ZScriptValue v) => v?.AsDouble() ?? 0.0;
        public static implicit operator string(ZScriptValue v) => v?.AsString() ?? "";

        // ------------------------------------------------------------------
        // Raw handle (internal — for passing back into C API calls)
        // ------------------------------------------------------------------
        internal IntPtr Raw => _raw;

        // ------------------------------------------------------------------
        // Lifetime
        // ------------------------------------------------------------------
        public void Dispose()
        {
            if (!_disposed)
            {
                _disposed = true;
                ZsNative.zs_value_free(_raw);
            }
        }

        public ZScriptValue Clone()
        {
            if (_disposed) return Nil;
            return new ZScriptValue(ZsNative.zs_value_clone(_raw));
        }

        public override string ToString() => AsString();
    }
}
