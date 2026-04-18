// ZsComponentBridge.cs — MonoBehaviour bridge that drives a ZScript class instance.
//
// Attach to a GameObject (or let ZScriptVM.AttachComponent() do it) to bind a
// ZScript class instance to Unity's lifecycle.  The bridge forwards each Unity
// message to the ZScript instance's method of the same name, if defined.
//
// Lifecycle methods forwarded:
//   Awake, Start, Update, FixedUpdate, LateUpdate,
//   OnEnable, OnDisable, OnDestroy
//
// Physics callbacks forwarded (3D):
//   OnCollisionEnter, OnCollisionStay, OnCollisionExit
//   OnTriggerEnter,   OnTriggerStay,   OnTriggerExit
//
// Physics callbacks forwarded (2D):
//   OnCollisionEnter2D, OnCollisionStay2D, OnCollisionExit2D
//   OnTriggerEnter2D,   OnTriggerStay2D,   OnTriggerExit2D
//
// The ZScript instance is a table (plain class instance, not an object handle).
// Set it via ZsComponentBridge.Instance before Start() is called, or use
// ZScriptVM.AttachComponent(go, className).
using System;
using System.Text;
using UnityEngine;

namespace ZScript
{
    public sealed class ZsComponentBridge : MonoBehaviour
    {
        // ----------------------------------------------------------------
        // Configuration — set before Start() fires.
        // ----------------------------------------------------------------
        /// <summary>The ZScript VM that owns the instance.</summary>
        public ZScriptVM ScriptVM { get; set; }

        /// <summary>
        /// The ZScript class instance (a table value).
        /// Owned by this bridge; freed in OnDestroy.
        /// </summary>
        public ZsValueHandle Instance { get; set; }

        // ----------------------------------------------------------------
        // Internal helpers
        // ----------------------------------------------------------------
        private IntPtr RawVM => ScriptVM != null ? ScriptVM.RawVM : IntPtr.Zero;

        // Call a no-arg method on the ZScript instance. Silently skips if
        // the method doesn't exist or the bridge isn't configured.
        private void Call(string method)
        {
            if (Instance == null || Instance.IsInvalid || RawVM == IntPtr.Zero) return;
            byte[] err = new byte[256];
            IntPtr outRaw;
            int ok = ZsNative.zs_vm_invoke_method(
                RawVM, Instance.Raw, method,
                0, Array.Empty<IntPtr>(),
                out outRaw, err, err.Length);
            if (outRaw != IntPtr.Zero) ZsNative.zs_value_free(outRaw);
            // Method-not-found is expected (optional methods) — only log real errors.
            if (ok == 0)
            {
                string msg = Encoding.UTF8.GetString(err).TrimEnd('\0');
                if (!msg.StartsWith("method '")) // skip "method 'X' not found"
                    Debug.LogError($"[ZScript] {name}.{method}: {msg}");
            }
        }

        // Call a method on the ZScript instance with one object-handle argument.
        // The argument is validity-checked before dispatch: if the Unity object
        // has been destroyed, the ZScript method is called with nil instead of
        // a dangling handle, so scripts can branch on nil gracefully.
        private void CallWithHandle(string method, object arg)
        {
            if (Instance == null || Instance.IsInvalid || RawVM == IntPtr.Zero) return;
            long id = ScriptVM.ObjectPool.Alloc(arg);
            // If the object was destroyed between Alloc and dispatch, pass nil.
            IntPtr argRaw = ScriptVM.ObjectPool.IsValid(id)
                ? ZsNative.zs_vm_push_object_handle(RawVM, id)
                : ZsNative.zs_value_nil();
            IntPtr[] argv = { argRaw };
            byte[] err = new byte[256];
            IntPtr outRaw;
            ZsNative.zs_vm_invoke_method(
                RawVM, Instance.Raw, method,
                1, argv,
                out outRaw, err, err.Length);
            ZsNative.zs_value_free(argRaw);
            if (outRaw != IntPtr.Zero) ZsNative.zs_value_free(outRaw);
        }

        // ----------------------------------------------------------------
        // Unity lifecycle
        // ----------------------------------------------------------------
        private void Awake() => Call("Awake");

        private void Start()
        {
            // Inject @unity.serialize field values before the ZScript start() runs
            // so scripts can read inspector-configured values from the very first line.
            GetComponent<ZsSerializedFields>()?.Apply(Instance);
            Call("Start");
        }
        private void Update()  => Call("Update");

        private void FixedUpdate() => Call("FixedUpdate");
        private void LateUpdate()  => Call("LateUpdate");
        private void OnEnable()    => Call("OnEnable");
        private void OnDisable()   => Call("OnDisable");

        private void OnDestroy()
        {
            Call("OnDestroy");
            // Free the ZScript instance value — the ZScript GC can now collect the table.
            Instance?.Dispose();
            Instance = null;
        }

        // ----------------------------------------------------------------
        // Physics — 3D
        // ----------------------------------------------------------------
        private void OnCollisionEnter(Collision collision) =>
            CallWithHandle("OnCollisionEnter", collision);

        private void OnCollisionStay(Collision collision) =>
            CallWithHandle("OnCollisionStay", collision);

        private void OnCollisionExit(Collision collision) =>
            CallWithHandle("OnCollisionExit", collision);

        private void OnTriggerEnter(Collider other) =>
            CallWithHandle("OnTriggerEnter", other);

        private void OnTriggerStay(Collider other) =>
            CallWithHandle("OnTriggerStay", other);

        private void OnTriggerExit(Collider other) =>
            CallWithHandle("OnTriggerExit", other);

        // ----------------------------------------------------------------
        // Physics — 2D
        // ----------------------------------------------------------------
        private void OnCollisionEnter2D(Collision2D collision) =>
            CallWithHandle("OnCollisionEnter2D", collision);

        private void OnCollisionStay2D(Collision2D collision) =>
            CallWithHandle("OnCollisionStay2D", collision);

        private void OnCollisionExit2D(Collision2D collision) =>
            CallWithHandle("OnCollisionExit2D", collision);

        private void OnTriggerEnter2D(Collider2D other) =>
            CallWithHandle("OnTriggerEnter2D", other);

        private void OnTriggerStay2D(Collider2D other) =>
            CallWithHandle("OnTriggerStay2D", other);

        private void OnTriggerExit2D(Collider2D other) =>
            CallWithHandle("OnTriggerExit2D", other);
    }
}
