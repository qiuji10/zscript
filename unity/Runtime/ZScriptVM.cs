// ZScriptVM.cs — MonoBehaviour that owns the ZScript VM instance.
// Attach to a GameObject in the scene.  Script files are loaded on Start();
// poll() is called each Update() to apply hotpatch reloads.
using System;
using System.Collections.Generic;
using System.Text;
using UnityEngine;

namespace ZScript
{
    /// <summary>
    /// MonoBehaviour that owns a ZScript VM for the lifetime of the scene.
    /// Exposes LoadFile(), LoadSource(), Call(), AddTag(), and the object pool
    /// to other components.
    /// </summary>
    public class ZScriptVM : MonoBehaviour
    {
        // ----------------------------------------------------------------
        // Inspector fields
        // ----------------------------------------------------------------
        [Tooltip("Script files to load on Start (paths relative to StreamingAssets).")]
        public List<string> startupScripts = new List<string>();

        [Tooltip("Enable live-reload (hotpatch) from the Scripts directory.")]
        public bool enableHotpatch = false;

        [Tooltip("Directory to watch for hotpatch (relative to Application.dataPath).")]
        public string hotpatchDir = "Scripts";

        [Tooltip("Tags to activate before loading scripts (e.g. 'unity', 'debug').")]
        public List<string> tags = new List<string>();

        // ----------------------------------------------------------------
        // Public API
        // ----------------------------------------------------------------
        /// <summary>The shared C# object pool for Unity ↔ ZScript object handles.</summary>
        public ZsObjectPool ObjectPool { get; } = new ZsObjectPool();

        public bool LoadFile(string path)
        {
            byte[] err = new byte[512];
            int ok = ZsNative.zs_vm_load_file(_vm, path, err, err.Length);
            if (ok == 0)
                Debug.LogError("[ZScript] LoadFile error: " + Encoding.UTF8.GetString(err).TrimEnd('\0'));
            return ok != 0;
        }

        public bool LoadSource(string name, string source)
        {
            byte[] err = new byte[512];
            int ok = ZsNative.zs_vm_load_source(_vm, name, source, err, err.Length);
            if (ok == 0)
                Debug.LogError("[ZScript] LoadSource error: " + Encoding.UTF8.GetString(err).TrimEnd('\0'));
            return ok != 0;
        }

        /// <summary>
        /// Call a named ZScript global function with the given arguments.
        /// Returns the first return value wrapped in a ZsValueHandle (caller disposes).
        /// Returns null on error.
        /// </summary>
        public ZsValueHandle Call(string name, params ZsValueHandle[] args)
        {
            IntPtr[] rawArgs = new IntPtr[args.Length];
            for (int i = 0; i < args.Length; ++i)
                rawArgs[i] = args[i].Raw;

            byte[] err = new byte[512];
            IntPtr outRaw;
            int ok = ZsNative.zs_vm_call(_vm, name, rawArgs.Length, rawArgs, out outRaw, err, err.Length);
            if (ok == 0)
            {
                Debug.LogError("[ZScript] Call '" + name + "' error: " +
                               Encoding.UTF8.GetString(err).TrimEnd('\0'));
                return null;
            }
            return new ZsValueHandle(outRaw);
        }

        public void AddTag(string tag)    => ZsNative.zs_vm_add_tag(_vm, tag);
        public void RemoveTag(string tag) => ZsNative.zs_vm_remove_tag(_vm, tag);
        public bool HasTag(string tag)    => ZsNative.zs_vm_has_tag(_vm, tag) != 0;

        /// <summary>Register a C# function as a ZScript global.</summary>
        public void RegisterFunction(string name, ZsNativeFn fn)
            => ZsNative.zs_vm_register_fn(_vm, name, fn);

        /// <summary>Create a ZScript proxy table for the given C# object.</summary>
        public ZsValueHandle WrapObject(object obj)
        {
            long id = ObjectPool.Alloc(obj);
            return new ZsValueHandle(ZsNative.zs_vm_push_object_handle(_vm, id));
        }

        /// <summary>Retrieve the C# object behind a ZScript proxy value.</summary>
        public object UnwrapObject(ZsValueHandle val)
        {
            long id = ZsNative.zs_vm_get_object_handle(_vm, val.Raw);
            return id >= 0 ? ObjectPool.Get(id) : null;
        }

        public T UnwrapObject<T>(ZsValueHandle val) where T : class
            => UnwrapObject(val) as T;

        /// <summary>Raw VM handle — for advanced binding code only.</summary>
        public IntPtr RawVM => _vm;

        // ----------------------------------------------------------------
        // Unity lifecycle
        // ----------------------------------------------------------------
        private IntPtr _vm = IntPtr.Zero;

        // Keep a strong reference to the delegate so it isn't GC'd.
        private ZsHandleReleaseFn _releaseFn;

        private void Awake()
        {
            _vm = ZsNative.zs_vm_new();
            ZsNative.zs_vm_open_stdlib(_vm);

            // Wire up the handle release callback.
            _releaseFn = OnHandleReleased;
            ZsNative.zs_vm_set_handle_release(_vm, _releaseFn);

            // Activate inspector-configured tags.
            foreach (string tag in tags)
                ZsNative.zs_vm_add_tag(_vm, tag);
        }

        private void Start()
        {
            // Load startup scripts.
            foreach (string script in startupScripts)
            {
                string fullPath = System.IO.Path.Combine(
                    Application.streamingAssetsPath, script);
                LoadFile(fullPath);
            }

            // Enable hotpatch if requested.
            if (enableHotpatch)
            {
                string dir = System.IO.Path.Combine(
                    Application.dataPath, hotpatchDir);
                if (ZsNative.zs_vm_enable_hotpatch(_vm, dir) == 0)
                    Debug.LogWarning("[ZScript] Hotpatch could not watch: " + dir);
            }
        }

        private void Update()
        {
            if (_vm != IntPtr.Zero)
            {
                int reloads = ZsNative.zs_vm_poll(_vm);
                if (reloads > 0)
                    Debug.Log("[ZScript] Hotpatch applied " + reloads + " reload(s).");
            }
        }

        private void OnDestroy()
        {
            if (_vm != IntPtr.Zero)
            {
                ZsNative.zs_vm_free(_vm);
                _vm = IntPtr.Zero;
            }
        }

        // ----------------------------------------------------------------
        // Handle release callback
        // ----------------------------------------------------------------
        private void OnHandleReleased(IntPtr /*vm*/ _, long handleId)
        {
            ObjectPool.Release(handleId);
        }
    }
}
