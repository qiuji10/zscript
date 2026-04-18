// ZsUnityEvent.cs — ZScript proxy wrappers for UnityEvent / UnityEvent<T> / UnityEvent<T0,T1>.
//
// Each wrapper creates a ZScript table with methods:
//   AddListener(fn)          → int   (listener id; pass to RemoveListener to unsubscribe)
//   RemoveListener(id)       → nil
//   RemoveAllListeners()     → nil
//   Invoke([args...])        → nil   (fires the underlying UnityEvent)
//
// Usage in generated binding code (e.g. Button.__index handler for "onClick"):
//   var ev = new ZsUnityEvent(vm, button.onClick);
//   return ev.CreateProxy();   // returns ZsValueHandle (table) to give back to ZScript
//
// The proxy is disposable; call Dispose() when the Unity object is destroyed.
using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Events;

namespace ZScript
{
    // =========================================================================
    // No-arg: UnityEvent
    // =========================================================================
    public sealed class ZsUnityEvent : IDisposable
    {
        private readonly IntPtr     _vm;
        private readonly UnityEvent _event;

        // Listener registry: id → (cloned ZsValueHandle, UnityAction)
        private readonly Dictionary<long, (ZsValueHandle handle, UnityAction action)> _listeners
            = new Dictionary<long, (ZsValueHandle, UnityAction)>();
        private long _nextId = 1;

        // Keep strong references to the native delegates so the GC can't collect them.
        private ZsNativeFn _addFn, _removeFn, _removeAllFn, _invokeFn;

        public ZsUnityEvent(IntPtr vm, UnityEvent unityEvent)
        {
            _vm    = vm;
            _event = unityEvent;
        }

        // Build a ZScript table proxy.  Caller owns the returned handle.
        public ZsValueHandle CreateProxy()
        {
            IntPtr tbl = ZsNative.zs_table_new();

            _addFn = (vm, argc, argv) =>
            {
                if (argc < 1) return ZsNative.zs_value_nil();
                long id = AddListener(argv[0]);
                return ZsNative.zs_value_int(id);
            };

            _removeFn = (vm, argc, argv) =>
            {
                if (argc >= 1) RemoveListener(ZsNative.zs_value_as_int(argv[0]));
                return ZsNative.zs_value_nil();
            };

            _removeAllFn = (vm, argc, argv) =>
            {
                RemoveAllListeners();
                return ZsNative.zs_value_nil();
            };

            _invokeFn = (vm, argc, argv) =>
            {
                _event.Invoke();
                return ZsNative.zs_value_nil();
            };

            ZsNative.zs_table_set_fn(tbl, "AddListener",       _addFn,       _vm);
            ZsNative.zs_table_set_fn(tbl, "RemoveListener",    _removeFn,    _vm);
            ZsNative.zs_table_set_fn(tbl, "RemoveAllListeners",_removeAllFn, _vm);
            ZsNative.zs_table_set_fn(tbl, "Invoke",            _invokeFn,    _vm);

            return new ZsValueHandle(tbl);
        }

        private long AddListener(IntPtr fnRaw)
        {
            var handle = new ZsValueHandle(ZsNative.zs_value_clone(fnRaw));
            long id = _nextId++;
            UnityAction action = () => InvokeClosure(handle);
            _listeners[id] = (handle, action);
            _event.AddListener(action);
            return id;
        }

        private void RemoveListener(long id)
        {
            if (!_listeners.TryGetValue(id, out var entry)) return;
            _event.RemoveListener(entry.action);
            entry.handle.Dispose();
            _listeners.Remove(id);
        }

        private void RemoveAllListeners()
        {
            foreach (var (handle, action) in _listeners.Values)
            {
                _event.RemoveListener(action);
                handle.Dispose();
            }
            _listeners.Clear();
        }

        private void InvokeClosure(ZsValueHandle fn)
        {
            byte[] err = new byte[256];
            IntPtr outRaw;
            ZsNative.zs_value_invoke(_vm, fn.Raw, 0, Array.Empty<IntPtr>(), out outRaw, err, err.Length);
            if (outRaw != IntPtr.Zero) ZsNative.zs_value_free(outRaw);
        }

        public void Dispose() => RemoveAllListeners();
    }

    // =========================================================================
    // One-arg: UnityEvent<T>
    // Marshals the T argument to ZScript via a user-supplied converter delegate.
    // =========================================================================
    public sealed class ZsUnityEvent<T> : IDisposable
    {
        private readonly IntPtr          _vm;
        private readonly UnityEvent<T>   _event;
        private readonly Func<T, IntPtr> _marshal; // T → ZsValue (caller must NOT free; we free after invoke)

        private readonly Dictionary<long, (ZsValueHandle handle, UnityAction<T> action)> _listeners
            = new Dictionary<long, (ZsValueHandle, UnityAction<T>)>();
        private long _nextId = 1;

        private ZsNativeFn _addFn, _removeFn, _removeAllFn, _invokeFn;

        /// <param name="marshal">Converts a T value into a ZsValue* to pass to ZScript.
        /// The returned IntPtr must be a freshly-allocated ZsValue (will be freed after use).</param>
        public ZsUnityEvent(IntPtr vm, UnityEvent<T> unityEvent, Func<T, IntPtr> marshal)
        {
            _vm      = vm;
            _event   = unityEvent;
            _marshal = marshal;
        }

        public ZsValueHandle CreateProxy()
        {
            IntPtr tbl = ZsNative.zs_table_new();

            _addFn = (vm, argc, argv) =>
            {
                if (argc < 1) return ZsNative.zs_value_nil();
                long id = AddListener(argv[0]);
                return ZsNative.zs_value_int(id);
            };

            _removeFn = (vm, argc, argv) =>
            {
                if (argc >= 1) RemoveListener(ZsNative.zs_value_as_int(argv[0]));
                return ZsNative.zs_value_nil();
            };

            _removeAllFn = (vm, argc, argv) =>
            {
                RemoveAllListeners();
                return ZsNative.zs_value_nil();
            };

            _invokeFn = (vm, argc, argv) =>
            {
                // Invoke with a default T — for direct ZScript→C# fire, marshal argv[0] back.
                // Typically unused; the event fires from C# side (button click etc.)
                return ZsNative.zs_value_nil();
            };

            ZsNative.zs_table_set_fn(tbl, "AddListener",       _addFn,       _vm);
            ZsNative.zs_table_set_fn(tbl, "RemoveListener",    _removeFn,    _vm);
            ZsNative.zs_table_set_fn(tbl, "RemoveAllListeners",_removeAllFn, _vm);
            ZsNative.zs_table_set_fn(tbl, "Invoke",            _invokeFn,    _vm);

            return new ZsValueHandle(tbl);
        }

        private long AddListener(IntPtr fnRaw)
        {
            var handle = new ZsValueHandle(ZsNative.zs_value_clone(fnRaw));
            long id = _nextId++;
            UnityAction<T> action = value => InvokeClosure(handle, value);
            _listeners[id] = (handle, action);
            _event.AddListener(action);
            return id;
        }

        private void RemoveListener(long id)
        {
            if (!_listeners.TryGetValue(id, out var entry)) return;
            _event.RemoveListener(entry.action);
            entry.handle.Dispose();
            _listeners.Remove(id);
        }

        private void RemoveAllListeners()
        {
            foreach (var (handle, action) in _listeners.Values)
            {
                _event.RemoveListener(action);
                handle.Dispose();
            }
            _listeners.Clear();
        }

        private void InvokeClosure(ZsValueHandle fn, T value)
        {
            IntPtr argRaw = _marshal(value);
            IntPtr[] argv = { argRaw };
            byte[] err = new byte[256];
            IntPtr outRaw;
            ZsNative.zs_value_invoke(_vm, fn.Raw, 1, argv, out outRaw, err, err.Length);
            ZsNative.zs_value_free(argRaw);
            if (outRaw != IntPtr.Zero) ZsNative.zs_value_free(outRaw);
        }

        public void Dispose() => RemoveAllListeners();
    }

    // =========================================================================
    // Two-arg: UnityEvent<T0, T1>
    // =========================================================================
    public sealed class ZsUnityEvent<T0, T1> : IDisposable
    {
        private readonly IntPtr               _vm;
        private readonly UnityEvent<T0, T1>   _event;
        private readonly Func<T0, IntPtr>     _marshal0;
        private readonly Func<T1, IntPtr>     _marshal1;

        private readonly Dictionary<long, (ZsValueHandle handle, UnityAction<T0, T1> action)> _listeners
            = new Dictionary<long, (ZsValueHandle, UnityAction<T0, T1>)>();
        private long _nextId = 1;

        private ZsNativeFn _addFn, _removeFn, _removeAllFn, _invokeFn;

        public ZsUnityEvent(IntPtr vm, UnityEvent<T0, T1> unityEvent,
                            Func<T0, IntPtr> marshal0, Func<T1, IntPtr> marshal1)
        {
            _vm       = vm;
            _event    = unityEvent;
            _marshal0 = marshal0;
            _marshal1 = marshal1;
        }

        public ZsValueHandle CreateProxy()
        {
            IntPtr tbl = ZsNative.zs_table_new();

            _addFn = (vm, argc, argv) =>
            {
                if (argc < 1) return ZsNative.zs_value_nil();
                long id = AddListener(argv[0]);
                return ZsNative.zs_value_int(id);
            };

            _removeFn = (vm, argc, argv) =>
            {
                if (argc >= 1) RemoveListener(ZsNative.zs_value_as_int(argv[0]));
                return ZsNative.zs_value_nil();
            };

            _removeAllFn = (vm, argc, argv) =>
            {
                RemoveAllListeners();
                return ZsNative.zs_value_nil();
            };

            _invokeFn = (vm, argc, argv) => ZsNative.zs_value_nil();

            ZsNative.zs_table_set_fn(tbl, "AddListener",       _addFn,       _vm);
            ZsNative.zs_table_set_fn(tbl, "RemoveListener",    _removeFn,    _vm);
            ZsNative.zs_table_set_fn(tbl, "RemoveAllListeners",_removeAllFn, _vm);
            ZsNative.zs_table_set_fn(tbl, "Invoke",            _invokeFn,    _vm);

            return new ZsValueHandle(tbl);
        }

        private long AddListener(IntPtr fnRaw)
        {
            var handle = new ZsValueHandle(ZsNative.zs_value_clone(fnRaw));
            long id = _nextId++;
            UnityAction<T0, T1> action = (v0, v1) => InvokeClosure(handle, v0, v1);
            _listeners[id] = (handle, action);
            _event.AddListener(action);
            return id;
        }

        private void RemoveListener(long id)
        {
            if (!_listeners.TryGetValue(id, out var entry)) return;
            _event.RemoveListener(entry.action);
            entry.handle.Dispose();
            _listeners.Remove(id);
        }

        private void RemoveAllListeners()
        {
            foreach (var (handle, action) in _listeners.Values)
            {
                _event.RemoveListener(action);
                handle.Dispose();
            }
            _listeners.Clear();
        }

        private void InvokeClosure(ZsValueHandle fn, T0 v0, T1 v1)
        {
            IntPtr a0 = _marshal0(v0);
            IntPtr a1 = _marshal1(v1);
            IntPtr[] argv = { a0, a1 };
            byte[] err = new byte[256];
            IntPtr outRaw;
            ZsNative.zs_value_invoke(_vm, fn.Raw, 2, argv, out outRaw, err, err.Length);
            ZsNative.zs_value_free(a0);
            ZsNative.zs_value_free(a1);
            if (outRaw != IntPtr.Zero) ZsNative.zs_value_free(outRaw);
        }

        public void Dispose() => RemoveAllListeners();
    }

    // =========================================================================
    // ZsActionEvent<T0, T1> — wraps a plain C# `event Action<T0,T1>` (not
    // UnityEvent).  Used for SceneManager.sceneLoaded, sceneUnloaded, etc.
    //
    // Because C# events do not expose a subscriber list, we subscribe once
    // with a single wrapper handler; that handler iterates _listeners.
    // =========================================================================
    public sealed class ZsActionEvent<T0, T1> : IDisposable
    {
        private readonly IntPtr           _vm;
        private readonly Func<T0, IntPtr> _marshal0;
        private readonly Func<T1, IntPtr> _marshal1;

        private readonly Dictionary<long, ZsValueHandle> _listeners
            = new Dictionary<long, ZsValueHandle>();
        private long _nextId = 1;

        // Stored so we can -= unsubscribe on Dispose.
        private readonly Action<T0, T1> _handler;

        // The subscribe/unsubscribe actions — provided by the caller so this
        // class can remain generic without knowing the event name.
        private readonly Action<Action<T0, T1>> _subscribe;
        private readonly Action<Action<T0, T1>> _unsubscribe;

        // GC pins for the proxy table delegates.
        private ZsNativeFn _addFn, _removeFn, _removeAllFn;

        public ZsActionEvent(IntPtr vm,
                             Action<Action<T0, T1>> subscribe,
                             Action<Action<T0, T1>> unsubscribe,
                             Func<T0, IntPtr> marshal0,
                             Func<T1, IntPtr> marshal1)
        {
            _vm          = vm;
            _marshal0    = marshal0;
            _marshal1    = marshal1;
            _subscribe   = subscribe;
            _unsubscribe = unsubscribe;
            _handler     = OnFired;
            subscribe(_handler);
        }

        public ZsValueHandle CreateProxy()
        {
            IntPtr tbl = ZsNative.zs_table_new();

            _addFn = (vm, argc, argv) =>
            {
                if (argc < 1) return ZsNative.zs_value_nil();
                var handle = new ZsValueHandle(ZsNative.zs_value_clone(argv[0]));
                long id = _nextId++;
                _listeners[id] = handle;
                return ZsNative.zs_value_int(id);
            };
            _removeFn = (vm, argc, argv) =>
            {
                if (argc >= 1) RemoveListener(ZsNative.zs_value_as_int(argv[0]));
                return ZsNative.zs_value_nil();
            };
            _removeAllFn = (vm, argc, argv) =>
            {
                RemoveAllListeners();
                return ZsNative.zs_value_nil();
            };

            ZsNative.zs_table_set_fn(tbl, "AddListener",        _addFn,       _vm);
            ZsNative.zs_table_set_fn(tbl, "RemoveListener",     _removeFn,    _vm);
            ZsNative.zs_table_set_fn(tbl, "RemoveAllListeners", _removeAllFn, _vm);
            return new ZsValueHandle(tbl);
        }

        private void OnFired(T0 v0, T1 v1)
        {
            IntPtr a0 = _marshal0(v0);
            IntPtr a1 = _marshal1(v1);
            IntPtr[] argv = { a0, a1 };
            byte[] err = new byte[256];
            foreach (var fn in _listeners.Values)
            {
                IntPtr outRaw;
                ZsNative.zs_value_invoke(_vm, fn.Raw, 2, argv, out outRaw, err, err.Length);
                if (outRaw != IntPtr.Zero) ZsNative.zs_value_free(outRaw);
            }
            ZsNative.zs_value_free(a0);
            ZsNative.zs_value_free(a1);
        }

        private void RemoveListener(long id)
        {
            if (_listeners.TryGetValue(id, out var h))
            {
                h.Dispose();
                _listeners.Remove(id);
            }
        }

        private void RemoveAllListeners()
        {
            foreach (var h in _listeners.Values) h.Dispose();
            _listeners.Clear();
        }

        public void Dispose()
        {
            _unsubscribe(_handler);
            RemoveAllListeners();
        }
    }

    // =========================================================================
    // Common marshal helpers for built-in Unity argument types.
    // Pass these as the 'marshal' delegate when constructing ZsUnityEvent<T>.
    // =========================================================================
    public static class ZsMarshal
    {
        public static IntPtr Bool(bool v)     => ZsNative.zs_value_bool(v ? 1 : 0);
        public static IntPtr Int(int v)       => ZsNative.zs_value_int(v);
        public static IntPtr Long(long v)     => ZsNative.zs_value_int(v);
        public static IntPtr Float(float v)   => ZsNative.zs_value_float(v);
        public static IntPtr Double(double v) => ZsNative.zs_value_float(v);
        public static IntPtr String(string v) => ZsNative.zs_value_string(v ?? "");

        // Vector3 → ZScript table {x, y, z}
        public static IntPtr Vector3(UnityEngine.Vector3 v)
        {
            IntPtr tbl = ZsNative.zs_table_new();
            using var x = new ZsValueHandle(ZsNative.zs_value_float(v.x));
            using var y = new ZsValueHandle(ZsNative.zs_value_float(v.y));
            using var z = new ZsValueHandle(ZsNative.zs_value_float(v.z));
            ZsNative.zs_table_set_value(tbl, "x", x.Raw);
            ZsNative.zs_table_set_value(tbl, "y", y.Raw);
            ZsNative.zs_table_set_value(tbl, "z", z.Raw);
            return tbl;
        }

        // Vector2 → ZScript table {x, y}
        public static IntPtr Vector2(UnityEngine.Vector2 v)
        {
            IntPtr tbl = ZsNative.zs_table_new();
            using var x = new ZsValueHandle(ZsNative.zs_value_float(v.x));
            using var y = new ZsValueHandle(ZsNative.zs_value_float(v.y));
            ZsNative.zs_table_set_value(tbl, "x", x.Raw);
            ZsNative.zs_table_set_value(tbl, "y", y.Raw);
            return tbl;
        }

        // Vector4 → ZScript table {x, y, z, w}
        public static IntPtr Vector4(UnityEngine.Vector4 v)
        {
            IntPtr tbl = ZsNative.zs_table_new();
            using var x = new ZsValueHandle(ZsNative.zs_value_float(v.x));
            using var y = new ZsValueHandle(ZsNative.zs_value_float(v.y));
            using var z = new ZsValueHandle(ZsNative.zs_value_float(v.z));
            using var w = new ZsValueHandle(ZsNative.zs_value_float(v.w));
            ZsNative.zs_table_set_value(tbl, "x", x.Raw);
            ZsNative.zs_table_set_value(tbl, "y", y.Raw);
            ZsNative.zs_table_set_value(tbl, "z", z.Raw);
            ZsNative.zs_table_set_value(tbl, "w", w.Raw);
            return tbl;
        }

        // Quaternion → ZScript table {x, y, z, w}
        public static IntPtr Quaternion(UnityEngine.Quaternion q)
        {
            IntPtr tbl = ZsNative.zs_table_new();
            using var x = new ZsValueHandle(ZsNative.zs_value_float(q.x));
            using var y = new ZsValueHandle(ZsNative.zs_value_float(q.y));
            using var z = new ZsValueHandle(ZsNative.zs_value_float(q.z));
            using var w = new ZsValueHandle(ZsNative.zs_value_float(q.w));
            ZsNative.zs_table_set_value(tbl, "x", x.Raw);
            ZsNative.zs_table_set_value(tbl, "y", y.Raw);
            ZsNative.zs_table_set_value(tbl, "z", z.Raw);
            ZsNative.zs_table_set_value(tbl, "w", w.Raw);
            return tbl;
        }

        // Color → ZScript table {r, g, b, a}
        public static IntPtr Color(UnityEngine.Color c)
        {
            IntPtr tbl = ZsNative.zs_table_new();
            using var r = new ZsValueHandle(ZsNative.zs_value_float(c.r));
            using var g = new ZsValueHandle(ZsNative.zs_value_float(c.g));
            using var b = new ZsValueHandle(ZsNative.zs_value_float(c.b));
            using var a = new ZsValueHandle(ZsNative.zs_value_float(c.a));
            ZsNative.zs_table_set_value(tbl, "r", r.Raw);
            ZsNative.zs_table_set_value(tbl, "g", g.Raw);
            ZsNative.zs_table_set_value(tbl, "b", b.Raw);
            ZsNative.zs_table_set_value(tbl, "a", a.Raw);
            return tbl;
        }
    }
}
