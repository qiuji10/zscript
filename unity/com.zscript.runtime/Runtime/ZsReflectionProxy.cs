// ZsReflectionProxy.cs — Reflection-based __index / __newindex for arbitrary C# objects.
//
// Enables Lua-like seamless access from ZScript:
//   obj.someProperty            → reads property/field via reflection
//   obj.someProperty = value    → writes property/field via reflection
//   obj.someMethod(args...)     → calls method via reflection
//
// Supported type conversions (C# ↔ ZScript):
//   bool                 ↔ bool
//   int / long           ↔ int
//   float / double       ↔ float
//   string               ↔ string
//   Color                ↔ {r, g, b, a}
//   Vector2              ↔ {x, y}
//   Vector3              ↔ {x, y, z}
//   Vector4 / Quaternion ↔ {x, y, z, w}
//   UnityEngine.Object   ↔ object handle (reflection proxy)
//   other reference type ↔ object handle (reflection proxy)
//
// IL2CPP note: reflection is preserved by default for UnityEngine assemblies.
// For custom assemblies use a link.xml to prevent managed stripping.
//
using System;
using System.Collections.Generic;
using System.Linq.Expressions;
using System.Reflection;
using System.Text;
using UnityEngine;

namespace ZScript
{
    /// <summary>
    /// Per-VM reflection proxy. Provides shared __index / __newindex delegates
    /// that dispatch property/field/method access via System.Reflection.
    /// </summary>
    public sealed class ZsReflectionProxy
    {
        // ── Shared static reflection caches ──────────────────────────────────────
        // These cache (Type, memberName) → MemberInfo and are safe across all VMs.
        private static readonly Dictionary<(Type, string), PropertyInfo> s_propCache
            = new Dictionary<(Type, string), PropertyInfo>();
        private static readonly Dictionary<(Type, string), FieldInfo> s_fieldCache
            = new Dictionary<(Type, string), FieldInfo>();
        private static readonly Dictionary<(Type, string), MethodInfo> s_methodCache
            = new Dictionary<(Type, string), MethodInfo>();
        private static readonly Dictionary<MethodInfo, Func<object, object[], object>> s_methodInvokerCache
            = new Dictionary<MethodInfo, Func<object, object[], object>>();

        // ── Per-VM state ──────────────────────────────────────────────────────────
        private readonly IntPtr      _rawVm;
        private readonly ZsObjectPool _pool;

        // Method dispatch delegates, cached per (Type, methodName).
        // Kept in a list so the GC never collects them while the C++ VM holds pointers.
        private readonly Dictionary<(Type, string), ZsNativeFn> _methodFnCache
            = new Dictionary<(Type, string), ZsNativeFn>();
        private readonly List<ZsNativeFn> _pins = new List<ZsNativeFn>();

        /// <summary>Shared __index metamethod for reflected objects.</summary>
        public ZsNativeFn IndexFn    { get; }
        /// <summary>Shared __newindex metamethod for reflected objects.</summary>
        public ZsNativeFn NewIndexFn { get; }

        public ZsReflectionProxy(IntPtr rawVm, ZsObjectPool pool)
        {
            _rawVm = rawVm;
            _pool  = pool;

            IndexFn    = CreateIndexFn();
            NewIndexFn = CreateNewIndexFn();
            _pins.Add(IndexFn);
            _pins.Add(NewIndexFn);
        }

        // ── Wrap a C# object with reflection proxy metamethods ────────────────────
        /// <summary>
        /// Allocate obj in the pool, create a ZScript proxy handle, and set
        /// reflection __index / __newindex on it.  Caller receives a new ZsValueBox*.
        /// </summary>
        public IntPtr Wrap(object obj)
        {
            if (obj == null) return ZsNative.zs_value_nil();
            long id = _pool.Alloc(obj);
            IntPtr proxy = ZsNative.zs_vm_push_object_handle(_rawVm, id);
            ZsNative.zs_vm_handle_set_index(_rawVm, proxy, IndexFn);
            ZsNative.zs_vm_handle_set_newindex(_rawVm, proxy, NewIndexFn);
            return proxy;
        }

        // ── __index delegate ──────────────────────────────────────────────────────
        private ZsNativeFn CreateIndexFn() => (vm, argc, argv) =>
        {
            if (argc < 2) return ZsNative.zs_value_nil();
            long id = ZsNative.zs_vm_get_object_handle(_rawVm, argv[0]);
            if (id < 0 || !_pool.IsValid(id)) return ZsNative.zs_value_nil();
            object obj = _pool.Get(id);
            if (obj == null) return ZsNative.zs_value_nil();
            string key = ReadString(argv[1]);
            return ResolveMember(obj, obj.GetType(), key);
        };

        // ── __newindex delegate ───────────────────────────────────────────────────
        private ZsNativeFn CreateNewIndexFn() => (vm, argc, argv) =>
        {
            if (argc < 3) return ZsNative.zs_value_nil();
            long id = ZsNative.zs_vm_get_object_handle(_rawVm, argv[0]);
            if (id < 0 || !_pool.IsValid(id)) return ZsNative.zs_value_nil();
            object obj = _pool.Get(id);
            if (obj == null) return ZsNative.zs_value_nil();
            string key = ReadString(argv[1]);
            SetMember(obj, obj.GetType(), key, argv[2]);
            return ZsNative.zs_value_nil();
        };

        // ── Member resolution ─────────────────────────────────────────────────────
        private IntPtr ResolveMember(object obj, Type type, string key)
        {
            // 1. Property (readable)
            var prop = GetProperty(type, key);
            if (prop != null && prop.CanRead)
            {
                try { return ToZsValue(prop.GetValue(obj), prop.PropertyType); }
                catch (Exception e) { LogError(key, e); return ZsNative.zs_value_nil(); }
            }

            // 2. Field
            var field = GetField(type, key);
            if (field != null)
            {
                try { return ToZsValue(field.GetValue(obj), field.FieldType); }
                catch (Exception e) { LogError(key, e); return ZsNative.zs_value_nil(); }
            }

            // 3. Method → return a callable ZsValue
            var method = GetMethod(type, key);
            if (method != null)
                return GetOrMakeMethodCallable(type, key, method);

            return ZsNative.zs_value_nil();
        }

        private void SetMember(object obj, Type type, string key, IntPtr val)
        {
            var prop = GetProperty(type, key);
            if (prop != null && prop.CanWrite)
            {
                try { prop.SetValue(obj, FromZsValue(val, prop.PropertyType)); }
                catch (Exception e) { LogError(key, e); }
                return;
            }
            var field = GetField(type, key);
            if (field != null)
            {
                try { field.SetValue(obj, FromZsValue(val, field.FieldType)); }
                catch (Exception e) { LogError(key, e); }
            }
        }

        // ── Method callable ───────────────────────────────────────────────────────
        // Returns a new ZsValueBox* wrapping a native callable for the method.
        // The delegate is cached per (Type,name) and GC-pinned in _pins.
        private IntPtr GetOrMakeMethodCallable(Type type, string key, MethodInfo method)
        {
            if (!_methodFnCache.TryGetValue((type, key), out var fn))
            {
                fn = CreateMethodDelegate(method);
                _methodFnCache[(type, key)] = fn;
                _pins.Add(fn);
            }
            // Each call to __index creates a fresh ZsValueBox; the inner shared_ptr
            // keeps the native Value alive in the VM register until called.
            return ZsNative.zs_vm_make_native_fn(_rawVm, key, fn);
        }

        private ZsNativeFn CreateMethodDelegate(MethodInfo method)
        {
            // argv[0] = self (the proxy table); argv[1..] = actual arguments.
            // ZScript CallMethod passes the receiver as the first argument.
            return (vm, argc, argv) =>
            {
                if (argc < 1) return ZsNative.zs_value_nil();
                long id = ZsNative.zs_vm_get_object_handle(_rawVm, argv[0]);
                if (id < 0) return ZsNative.zs_value_nil();
                object self = _pool.Get(id);
                if (self == null) return ZsNative.zs_value_nil();

                ParameterInfo[] parms = method.GetParameters();
                object[] args = new object[parms.Length];
                for (int i = 0; i < parms.Length; i++)
                    args[i] = (i + 1 < argc)
                        ? FromZsValue(argv[i + 1], parms[i].ParameterType)
                        : GetDefault(parms[i].ParameterType);
                try
                {
                    object result = InvokeMethod(method, self, args);
                    return method.ReturnType == typeof(void)
                        ? ZsNative.zs_value_nil()
                        : ToZsValue(result, method.ReturnType);
                }
                catch (Exception e)
                {
                    Debug.LogError($"[ZsReflection] {method.DeclaringType?.Name}.{method.Name}: {e.InnerException?.Message ?? e.Message}");
                    return ZsNative.zs_value_nil();
                }
            };
        }

        // ── Type conversion: C# → ZsValue ────────────────────────────────────────
        internal IntPtr ToZsValue(object value, Type type)
        {
            if (value == null) return ZsNative.zs_value_nil();

            // Primitive types
            if (type == typeof(bool)   || value is bool)   return ZsNative.zs_value_bool((bool)value ? 1 : 0);
            if (type == typeof(int))    return ZsNative.zs_value_int((int)value);
            if (type == typeof(uint))   return ZsNative.zs_value_int((uint)value);
            if (type == typeof(long))   return ZsNative.zs_value_int((long)value);
            if (type == typeof(ulong))  return ZsNative.zs_value_int((long)(ulong)value);
            if (type == typeof(short))  return ZsNative.zs_value_int((short)value);
            if (type == typeof(ushort)) return ZsNative.zs_value_int((ushort)value);
            if (type == typeof(byte))   return ZsNative.zs_value_int((byte)value);
            if (type == typeof(sbyte))  return ZsNative.zs_value_int((sbyte)value);
            if (type == typeof(float))  return ZsNative.zs_value_float((float)value);
            if (type == typeof(double)) return ZsNative.zs_value_float((double)value);
            if (type == typeof(string)) return ZsNative.zs_value_string((string)value ?? "");

            // Unity struct types
            if (value is Color   c)  return MakeColorTable(c);
            if (value is Vector2 v2) return ZsMarshal.Vector2(v2);
            if (value is Vector3 v3) return ZsMarshal.Vector3(v3);
            if (value is Vector4 v4) return MakeVector4Table(v4);
            if (value is Quaternion q) return MakeQuaternionTable(q);

            // Enum → int
            if (type.IsEnum) return ZsNative.zs_value_int(Convert.ToInt64(value));

            // Unity objects and other reference types → reflection proxy handle
            return Wrap(value);
        }

        // ── Type conversion: ZsValue → C# ────────────────────────────────────────
        internal object FromZsValue(IntPtr val, Type targetType)
        {
            if (val == IntPtr.Zero) return GetDefault(targetType);
            ZsType zt = ZsNative.zs_value_type(val);

            if (targetType == typeof(bool))   return ZsNative.zs_value_as_bool(val) != 0;
            if (targetType == typeof(int))    return (int)ZsNative.zs_value_as_int(val);
            if (targetType == typeof(uint))   return (uint)ZsNative.zs_value_as_int(val);
            if (targetType == typeof(long))   return ZsNative.zs_value_as_int(val);
            if (targetType == typeof(ulong))  return (ulong)ZsNative.zs_value_as_int(val);
            if (targetType == typeof(short))  return (short)ZsNative.zs_value_as_int(val);
            if (targetType == typeof(ushort)) return (ushort)ZsNative.zs_value_as_int(val);
            if (targetType == typeof(byte))   return (byte)ZsNative.zs_value_as_int(val);
            if (targetType == typeof(sbyte))  return (sbyte)ZsNative.zs_value_as_int(val);
            if (targetType == typeof(float))  return (float)ZsNative.zs_value_as_float(val);
            if (targetType == typeof(double)) return ZsNative.zs_value_as_float(val);
            if (targetType == typeof(string)) return ReadString(val);

            // Unity struct types from {x,y,z,...} tables
            if (targetType == typeof(Color))      return ReadColor(val);
            if (targetType == typeof(Vector2))    return ReadVector2(val);
            if (targetType == typeof(Vector3))    return ReadVector3(val);
            if (targetType == typeof(Vector4))    return ReadVector4(val);
            if (targetType == typeof(Quaternion)) return ReadQuaternion(val);

            // Enum from int
            if (targetType.IsEnum)
                return Enum.ToObject(targetType, ZsNative.zs_value_as_int(val));

            // Object handle → retrieve from pool
            if (zt == ZsType.Object || zt == ZsType.Table)
            {
                long id = ZsNative.zs_vm_get_object_handle(_rawVm, val);
                if (id >= 0)
                {
                    object obj = _pool.Get(id);
                    if (obj != null && targetType.IsAssignableFrom(obj.GetType()))
                        return obj;
                }
            }

            return GetDefault(targetType);
        }

        // ── Reflection member lookup with caching ─────────────────────────────────
        private static PropertyInfo GetProperty(Type type, string name)
        {
            var key = (type, name);
            if (s_propCache.TryGetValue(key, out var pi)) return pi;
            pi = type.GetProperty(name, BindingFlags.Public | BindingFlags.Instance);
            s_propCache[key] = pi;
            return pi;
        }

        private static FieldInfo GetField(Type type, string name)
        {
            var key = (type, name);
            if (s_fieldCache.TryGetValue(key, out var fi)) return fi;
            fi = type.GetField(name, BindingFlags.Public | BindingFlags.Instance);
            s_fieldCache[key] = fi;
            return fi;
        }

        private static MethodInfo GetMethod(Type type, string name)
        {
            var key = (type, name);
            if (s_methodCache.TryGetValue(key, out var mi)) return mi;
            // Pick the simplest non-special public instance method with this name.
            MethodInfo[] methods = type.GetMethods(BindingFlags.Public | BindingFlags.Instance);
            for (int i = 0; i < methods.Length; i++)
            {
                var cand = methods[i];
                if (cand.Name != name || cand.IsSpecialName) continue;
                if (mi == null || cand.GetParameters().Length < mi.GetParameters().Length)
                    mi = cand;
            }
            s_methodCache[key] = mi;
            return mi;
        }

        private static object InvokeMethod(MethodInfo method, object target, object[] args)
        {
            if (!s_methodInvokerCache.TryGetValue(method, out var invoker))
            {
                invoker = BuildMethodInvoker(method);
                s_methodInvokerCache[method] = invoker;
            }

            if (invoker != null)
                return invoker(target, args);

            return method.Invoke(target, args);
        }

        private static Func<object, object[], object> BuildMethodInvoker(MethodInfo method)
        {
            try
            {
                var targetParam = Expression.Parameter(typeof(object), "target");
                var argsParam = Expression.Parameter(typeof(object[]), "args");

                var callArgs = new Expression[method.GetParameters().Length];
                ParameterInfo[] parms = method.GetParameters();
                for (int i = 0; i < parms.Length; i++)
                {
                    var argExpr = Expression.ArrayIndex(argsParam, Expression.Constant(i));
                    callArgs[i] = Expression.Convert(argExpr, parms[i].ParameterType);
                }

                var instance = Expression.Convert(targetParam, method.DeclaringType);
                var call = Expression.Call(instance, method, callArgs);

                Expression body = method.ReturnType == typeof(void)
                    ? Expression.Block(call, Expression.Constant(null, typeof(object)))
                    : Expression.Convert(call, typeof(object));

                return Expression.Lambda<Func<object, object[], object>>(body, targetParam, argsParam).Compile();
            }
            catch
            {
                return null;
            }
        }

        // ── Struct table helpers ──────────────────────────────────────────────────
        private static float Fld(IntPtr tbl, string k) =>
            (float)ZsNative.zs_value_as_float(ZsNative.zs_vm_handle_get_field(IntPtr.Zero, tbl, k));

        private static Color     ReadColor(IntPtr v)     => new Color(Fld(v,"r"), Fld(v,"g"), Fld(v,"b"), Fld(v,"a"));
        private static Vector2   ReadVector2(IntPtr v)   => new Vector2(Fld(v,"x"), Fld(v,"y"));
        private static Vector3   ReadVector3(IntPtr v)   => new Vector3(Fld(v,"x"), Fld(v,"y"), Fld(v,"z"));
        private static Vector4   ReadVector4(IntPtr v)   => new Vector4(Fld(v,"x"), Fld(v,"y"), Fld(v,"z"), Fld(v,"w"));
        private static Quaternion ReadQuaternion(IntPtr v) => new Quaternion(Fld(v,"x"), Fld(v,"y"), Fld(v,"z"), Fld(v,"w"));

        private static IntPtr MakeColorTable(Color c)
        {
            IntPtr tbl = ZsNative.zs_table_new();
            SetF(tbl, "r", c.r); SetF(tbl, "g", c.g); SetF(tbl, "b", c.b); SetF(tbl, "a", c.a);
            return tbl;
        }

        private static IntPtr MakeVector4Table(Vector4 v)
        {
            IntPtr tbl = ZsNative.zs_table_new();
            SetF(tbl, "x", v.x); SetF(tbl, "y", v.y); SetF(tbl, "z", v.z); SetF(tbl, "w", v.w);
            return tbl;
        }

        private static IntPtr MakeQuaternionTable(Quaternion q)
        {
            IntPtr tbl = ZsNative.zs_table_new();
            SetF(tbl, "x", q.x); SetF(tbl, "y", q.y); SetF(tbl, "z", q.z); SetF(tbl, "w", q.w);
            return tbl;
        }

        private static void SetF(IntPtr tbl, string k, float v)
        {
            IntPtr fv = ZsNative.zs_value_float(v);
            ZsNative.zs_table_set_value(tbl, k, fv);
            ZsNative.zs_value_free(fv);
        }

        // ── Utility ───────────────────────────────────────────────────────────────
        internal static string ReadString(IntPtr v)
        {
            int len = ZsNative.zs_value_as_string(v, null, 0);
            byte[] buf = new byte[len + 1];
            ZsNative.zs_value_as_string(v, buf, buf.Length);
            return Encoding.UTF8.GetString(buf, 0, len);
        }

        private static object GetDefault(Type t)
        {
            if (t == null || !t.IsValueType) return null;
            return Activator.CreateInstance(t);
        }

        private static void LogError(string member, Exception e) =>
            Debug.LogError($"[ZsReflection] member '{member}': {e.InnerException?.Message ?? e.Message}");
    }
}
