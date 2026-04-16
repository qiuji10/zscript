// ZsObjectPool.cs — Maps monotonically increasing integer IDs ↔ C# objects.
// The pool holds a strong reference to each object so the C# GC cannot collect
// it while ZScript holds the corresponding proxy table.  When ZScript GC drops
// the proxy (gc_hook fires → zs_vm_set_handle_release callback), Release() is
// called and the strong reference is dropped.
using System;
using System.Collections.Generic;
using UnityEngine.Scripting;

namespace ZScript
{
    /// <summary>
    /// Thread-safe integer-keyed object pool used to pass C# object references
    /// across the P/Invoke boundary as opaque integer handles.
    /// </summary>
    [Preserve]
    public sealed class ZsObjectPool
    {
        private readonly object _lock = new object();
        private readonly Dictionary<long, object> _idToObj  = new Dictionary<long, object>();
        private readonly Dictionary<object, long> _objToId  = new Dictionary<object, long>();
        private long _nextId = 1; // 0 reserved for "invalid"

        // ----------------------------------------------------------------
        // Alloc — intern an object; returns existing id if already pooled.
        // ----------------------------------------------------------------
        public long Alloc(object obj)
        {
            if (obj == null) throw new ArgumentNullException(nameof(obj));
            lock (_lock)
            {
                if (_objToId.TryGetValue(obj, out long existing))
                    return existing;

                long id = _nextId++;
                _idToObj[id]  = obj;
                _objToId[obj] = id;
                return id;
            }
        }

        // ----------------------------------------------------------------
        // Get — retrieve the object for an id. Returns null if not found.
        // ----------------------------------------------------------------
        public object Get(long id)
        {
            lock (_lock)
            {
                _idToObj.TryGetValue(id, out object obj);
                return obj;
            }
        }

        public T Get<T>(long id) where T : class
            => Get(id) as T;

        // ----------------------------------------------------------------
        // Release — drop the strong reference. Called from handle_release.
        // ----------------------------------------------------------------
        public void Release(long id)
        {
            lock (_lock)
            {
                if (_idToObj.TryGetValue(id, out object obj))
                {
                    _idToObj.Remove(id);
                    if (obj != null) _objToId.Remove(obj); // null = already invalidated
                }
            }
        }

        // ----------------------------------------------------------------
        // Invalidate — mark a handle as destroyed without fully releasing it.
        // Subsequent Get() calls return null; IsValid() returns false.
        // The id remains in _idToObj so the GC-hook Release() still cleans up.
        // ----------------------------------------------------------------
        public void Invalidate(long id)
        {
            lock (_lock)
            {
                if (_idToObj.TryGetValue(id, out object obj) && obj != null)
                {
                    _objToId.Remove(obj);   // detach reverse mapping
                    _idToObj[id] = null;    // keep slot alive, mark destroyed
                }
            }
        }

        // ----------------------------------------------------------------
        // IsValid — true if the handle exists AND the object has not been
        // destroyed. For UnityEngine.Object subtypes, uses Unity's overloaded
        // == null which returns true for destroyed native objects.
        // ----------------------------------------------------------------
        public bool IsValid(long id)
        {
            lock (_lock)
            {
                if (!_idToObj.TryGetValue(id, out object obj)) return false;
                if (obj == null) return false;
                // Unity overloads == null to detect native-side destruction.
                if (obj is UnityEngine.Object uObj)
                    return uObj != null;
                return true;
            }
        }

        // ----------------------------------------------------------------
        // Count — for diagnostics.
        // ----------------------------------------------------------------
        public int Count
        {
            get { lock (_lock) { return _idToObj.Count; } }
        }
    }
}
