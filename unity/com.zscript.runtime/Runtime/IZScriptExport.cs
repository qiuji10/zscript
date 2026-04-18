// IZScriptExport.cs — Interface for auto-registering ZScript bindings.
//
// Implement IZScriptExport on any class to have it auto-discovered and called
// during ZScriptVM.Awake(). No manual wiring is needed.
//
// The generated binding classes produced by Tools > ZScript > Generate Export Bindings
// implement this interface automatically, so they are picked up on every VM startup.
//
// Hand-written binding example:
//
//   public sealed class MyManagerBinding : IZScriptExport
//   {
//       private ZsNativeFn _fnReset;          // hold delegate alive
//       public void Register(ZScriptVM vm)
//       {
//           var tbl = new ZsValueHandle(ZsNative.zs_table_new());
//           _fnReset = (_, __, ___) => { MyManager.Reset(); return ZsNative.zs_value_nil(); };
//           ZsNative.zs_table_set_fn(tbl.Raw, "Reset", _fnReset, vm.RawVM);
//           ZsNative.zs_vm_set_global(vm.RawVM, "MyManager", tbl.Raw);
//           tbl.Dispose();
//       }
//   }
using System;

namespace ZScript
{
    /// <summary>
    /// Implement this interface to register ZScript bindings automatically when any
    /// <see cref="ZScriptVM"/> starts. Implementations are discovered by reflection
    /// across all non-engine assemblies in <see cref="ZScriptVM.Awake"/>.
    /// </summary>
    public interface IZScriptExport
    {
        /// <summary>
        /// Register bindings into <paramref name="vm"/> — add globals, functions, and
        /// table namespaces that ZScript scripts can access.
        /// Called once per VM instance during Awake().
        /// </summary>
        void Register(ZScriptVM vm);
    }

    /// <summary>
    /// Optional companion interface for generated or hand-written instance wrappers.
    /// Implementations create typed proxy handles for a specific C# target type.
    /// </summary>
    public interface IZScriptInstanceExport
    {
        /// <summary>The C# type this exporter can wrap.</summary>
        Type TargetType { get; }

        /// <summary>
        /// Create a raw ZScript proxy value for <paramref name="obj"/>.
        /// The returned pointer must be a heap-allocated ZsValue owned by the caller.
        /// </summary>
        IntPtr WrapRaw(ZScriptVM vm, object obj);
    }

    /// <summary>
    /// Marks a class as produced by the ZScript Export Generator codegen tool.
    /// This attribute is informational — it does not affect runtime behaviour.
    /// Hand-written bindings should NOT use this attribute.
    /// </summary>
    [AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
    public sealed class ZScriptGeneratedBindingAttribute : Attribute { }
}
