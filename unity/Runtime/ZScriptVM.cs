// ZScriptVM.cs — MonoBehaviour that owns the ZScript VM instance.
// Attach to a GameObject in the scene.  Script files are loaded on Start();
// poll() is called each Update() to apply hotpatch reloads.
using System;
using System.Collections;
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
        [Tooltip("ZScriptModule assets to load on Start (loaded in list order).")]
        public List<ZScriptModule> startupModules = new List<ZScriptModule>();

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

        // ----------------------------------------------------------------
        // Coroutine bridge
        // ----------------------------------------------------------------
        /// <summary>
        /// Start a named ZScript function as a Unity coroutine.
        /// Inside the ZScript function use <c>coroutine.yield(WaitForSeconds(n))</c>
        /// etc. to suspend until the Unity yield instruction completes.
        /// </summary>
        /// <returns>A Unity <see cref="Coroutine"/> handle for
        /// <see cref="StopZsCoroutine"/>.</returns>
        public Coroutine StartZsCoroutine(string fnName)
        {
            using var closureHandle = new ZsValueHandle(ZsNative.zs_vm_get_global(_vm, fnName));
            if (closureHandle.Type == ZsType.Nil)
            {
                Debug.LogError($"[ZScript] StartZsCoroutine: '{fnName}' not found.");
                return null;
            }
            var coHandle = new ZsValueHandle(ZsNative.zs_coroutine_create(_vm, closureHandle.Raw));
            if (coHandle.Type != ZsType.Coroutine)
            {
                coHandle.Dispose();
                Debug.LogError($"[ZScript] StartZsCoroutine: '{fnName}' is not a function.");
                return null;
            }
            return StartCoroutine(new ZsCoroutineBridge(_vm, coHandle));
        }

        /// <summary>Start a ZScript coroutine value directly as a Unity coroutine.</summary>
        public Coroutine StartZsCoroutine(ZsValueHandle coVal)
        {
            if (coVal == null || coVal.IsInvalid) return null;
            return StartCoroutine(new ZsCoroutineBridge(_vm, coVal.Clone()));
        }

        /// <summary>Stop a running ZScript coroutine by its Unity handle.</summary>
        public void StopZsCoroutine(Coroutine co)
        {
            if (co != null) StopCoroutine(co);
        }

        // ----------------------------------------------------------------
        // Component bridge
        // ----------------------------------------------------------------
        /// <summary>
        /// Instantiate a ZScript class by name and attach a
        /// <see cref="ZsComponentBridge"/> to the given GameObject.
        /// The ZScript class constructor is called with no arguments.
        /// </summary>
        /// <param name="go">Target GameObject.</param>
        /// <param name="className">
        ///   Name of the ZScript global that holds the class table.
        /// </param>
        /// <returns>The attached <see cref="ZsComponentBridge"/>, or null on error.</returns>
        public ZsComponentBridge AttachComponent(GameObject go, string className)
        {
            // Get the class table.
            using var classTbl = new ZsValueHandle(ZsNative.zs_vm_get_global(_vm, className));
            if (classTbl.Type == ZsType.Nil)
            {
                Debug.LogError($"[ZScript] AttachComponent: class '{className}' not found.");
                return null;
            }

            // Instantiate: call the class table as a constructor (__call metamethod).
            byte[] err = new byte[256];
            IntPtr outRaw;
            int ok = ZsNative.zs_value_invoke(
                _vm, classTbl.Raw,
                0, Array.Empty<IntPtr>(),
                out outRaw, err, err.Length);
            if (ok == 0 || outRaw == IntPtr.Zero)
            {
                Debug.LogError($"[ZScript] AttachComponent: '{className}()' failed: " +
                               System.Text.Encoding.UTF8.GetString(err).TrimEnd('\0'));
                return null;
            }
            var instance = new ZsValueHandle(outRaw);

            var bridge = go.AddComponent<ZsComponentBridge>();
            bridge.ScriptVM = this;
            bridge.Instance = instance;
            return bridge;
        }

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

        // Strong references — prevent GC from collecting delegates held only by C++.
        private ZsHandleReleaseFn _releaseFn;
        private ZsNativeFn        _startCoFn;
        private ZsNativeFn        _stopCoFn;

        // ZScript source that defines the yield-descriptor factory functions.
        // These are loaded once in Awake() so any script can call them.
        private const string YieldHelperSource = @"
fn WaitForSeconds(seconds) {
    return { __yield_type: ""WaitForSeconds"", seconds: seconds }
}
fn WaitForEndOfFrame() {
    return { __yield_type: ""WaitForEndOfFrame"" }
}
fn WaitForFixedUpdate() {
    return { __yield_type: ""WaitForFixedUpdate"" }
}
fn WaitUntil(pred) {
    return { __yield_type: ""WaitUntil"", fn: pred }
}
fn WaitWhile(pred) {
    return { __yield_type: ""WaitWhile"", fn: pred }
}
";

        // Register yield helpers and StartCoroutine / StopCoroutine into the VM.
        private void RegisterYieldHelpers()
        {
            // Load pure-ZScript yield descriptor factories.
            byte[] err = new byte[256];
            int ok = ZsNative.zs_vm_load_source(
                _vm, "__yield_helpers__", YieldHelperSource, err, err.Length);
            if (ok == 0)
                Debug.LogError("[ZScript] RegisterYieldHelpers: " +
                               Encoding.UTF8.GetString(err).TrimEnd('\0'));

            // StartCoroutine(fn) — takes a ZScript closure, wraps it in a
            // ZsCoroutineBridge, and drives it through Unity's scheduler.
            _startCoFn = (vm, argc, argv) =>
            {
                if (argc < 1) return ZsNative.zs_value_nil();
                var coHandle = new ZsValueHandle(ZsNative.zs_coroutine_create(vm, argv[0]));
                if (coHandle.Type != ZsType.Coroutine)
                {
                    coHandle.Dispose();
                    return ZsNative.zs_value_nil();
                }
                var bridge = new ZsCoroutineBridge(vm, coHandle);
                Coroutine co = StartCoroutine(bridge);
                long id = ObjectPool.Alloc(co);
                return ZsNative.zs_value_int(id);
            };
            ZsNative.zs_vm_register_fn(_vm, "StartCoroutine", _startCoFn);

            // StopCoroutine(handle) — takes the integer handle returned by
            // StartCoroutine and cancels the Unity coroutine.
            _stopCoFn = (vm, argc, argv) =>
            {
                if (argc < 1) return ZsNative.zs_value_nil();
                long id = ZsNative.zs_value_as_int(argv[0]);
                var co = ObjectPool.Get<Coroutine>(id);
                if (co != null)
                {
                    ObjectPool.Release(id);
                    StopCoroutine(co);
                }
                return ZsNative.zs_value_nil();
            };
            ZsNative.zs_vm_register_fn(_vm, "StopCoroutine", _stopCoFn);
        }

        private void Awake()
        {
            _vm = ZsNative.zs_vm_new();
            ZsNative.zs_vm_open_stdlib(_vm);

            // Wire up the handle release callback.
            _releaseFn = OnHandleReleased;
            ZsNative.zs_vm_set_handle_release(_vm, _releaseFn);

            // Register yield helpers + StartCoroutine / StopCoroutine globals.
            RegisterYieldHelpers();

            // Activate inspector-configured tags.
            foreach (string tag in tags)
                ZsNative.zs_vm_add_tag(_vm, tag);
        }

        private void Start()
        {
            // Load ScriptableObject module assets first.
            foreach (ZScriptModule mod in startupModules)
                mod?.LoadInto(this);

            // Then load raw file paths (StreamingAssets).
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
