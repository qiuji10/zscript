// ZsAdapterRegistry.cs — @unity.adapter class registry.
//
// Adapter classes wrap C# objects in a ZScript-friendly table API.  Instead of
// exposing a raw proxy (with a numeric __handle), the script receives a fully
// typed ZScript table whose methods and properties are defined in ZScript.
//
// ZScript usage:
//
//   @unity.adapter
//   class CameraAdapter {
//       fn new(proxy) { self._cam = proxy }
//       fn fieldOfView()              { return self._cam.fieldOfView }
//       fn setFieldOfView(fov)        { self._cam.fieldOfView = fov }
//   }
//
// C# usage — after scripts are loaded, call Refresh() once, then WrapAdapted:
//
//   vm.AdapterRegistry.Refresh(vm);                // scan loaded scripts
//   vm.AdapterRegistry.Register("Camera", "CameraAdapter"); // or manual override
//   ZsValueHandle adapted = vm.WrapAdapted(camera);
//
// The registry matches by C# type name.  Override via Register() when the class
// names diverge, or when a single adapter covers multiple C# types.
using System;
using System.Collections.Generic;
using System.Text;
using UnityEngine;

namespace ZScript
{
    /// <summary>
    /// Maintains a map from C# type names to <c>@unity.adapter</c>-annotated
    /// ZScript class names and uses them to instantiate adapters on demand.
    /// </summary>
    public sealed class ZsAdapterRegistry
    {
        // C# simple type name → ZScript adapter class name
        private readonly Dictionary<string, string> _typeToAdapter =
            new Dictionary<string, string>(StringComparer.Ordinal);

        // ----------------------------------------------------------------
        // Population
        // ----------------------------------------------------------------

        /// <summary>
        /// Scan the VM for all <c>@unity.adapter</c>-annotated classes and register
        /// them using the class name as the C# type name (convention-over-configuration).
        /// Existing manual registrations are preserved; call <see cref="Clear"/> first
        /// to start fresh.  Safe to call multiple times (e.g. after hotpatch reload).
        /// </summary>
        public void Refresh(ZScriptVM vm)
        {
            string[] classes = vm.FindAnnotatedClasses("unity", "adapter");
            foreach (string cls in classes)
            {
                if (!_typeToAdapter.ContainsKey(cls))
                    _typeToAdapter[cls] = cls;
            }
        }

        /// <summary>
        /// Manually map <paramref name="csharpTypeName"/> to a ZScript adapter class.
        /// Overrides any convention-based auto-mapping from <see cref="Refresh"/>.
        /// </summary>
        public void Register(string csharpTypeName, string zsAdapterClass)
        {
            if (string.IsNullOrEmpty(csharpTypeName) || string.IsNullOrEmpty(zsAdapterClass))
                throw new ArgumentException("Type name and adapter class name must not be null or empty.");
            _typeToAdapter[csharpTypeName] = zsAdapterClass;
        }

        /// <summary>Remove the adapter mapping for <paramref name="csharpTypeName"/>.</summary>
        public void Unregister(string csharpTypeName) => _typeToAdapter.Remove(csharpTypeName);

        /// <summary>Remove all adapter mappings.</summary>
        public void Clear() => _typeToAdapter.Clear();

        /// <summary>
        /// Returns true if an adapter is registered for <paramref name="csharpTypeName"/>.
        /// </summary>
        public bool HasAdapter(string csharpTypeName)
            => _typeToAdapter.ContainsKey(csharpTypeName);

        // ----------------------------------------------------------------
        // Wrapping
        // ----------------------------------------------------------------

        /// <summary>
        /// Wrap <paramref name="obj"/> using its registered adapter class if one exists;
        /// otherwise falls back to <see cref="ZScriptVM.WrapSmart"/>.
        ///
        /// The returned <see cref="ZsValueHandle"/> is owned by the caller and must be
        /// disposed (or transferred to a ZScript global / table field).
        /// </summary>
        internal IntPtr TryWrapRaw(ZScriptVM vm, object obj)
        {
            if (obj == null) return IntPtr.Zero;

            string typeName = obj.GetType().Name;
            if (!_typeToAdapter.TryGetValue(typeName, out string adapterClass))
                return IntPtr.Zero;

            // Retrieve the adapter class table from the VM.
            using var classTbl = new ZsValueHandle(ZsNative.zs_vm_get_global(vm.RawVM, adapterClass));
            if (classTbl.Type == ZsType.Nil)
            {
                Debug.LogWarning($"[ZScript] ZsAdapterRegistry: adapter class '{adapterClass}' " +
                                 $"not found in VM — falling back to plain proxy.");
                return IntPtr.Zero;
            }

            // Create the raw C# proxy, then call the adapter constructor with it.
            // The adapter's new(proxy) receives the proxy and stores it in self.
            using var proxy = vm.WrapObject(obj);
            byte[] err = new byte[256];
            int ok = ZsNative.zs_value_invoke(
                vm.RawVM, classTbl.Raw,
                1, new[] { proxy.Raw },
                out IntPtr outRaw, err, err.Length);

            if (ok == 0 || outRaw == IntPtr.Zero)
            {
                string msg = Encoding.UTF8.GetString(err).TrimEnd('\0');
                Debug.LogWarning($"[ZScript] ZsAdapterRegistry: '{adapterClass}()' failed: {msg}. " +
                                 $"Falling back to plain proxy.");
                return IntPtr.Zero;
            }
            return outRaw;
        }

        public ZsValueHandle Wrap(ZScriptVM vm, object obj)
        {
            if (obj == null) return ZsValueHandle.Nil();
            IntPtr raw = TryWrapRaw(vm, obj);
            if (raw != IntPtr.Zero) return new ZsValueHandle(raw);
            return vm.WrapSmart(obj);
        }
    }
}
