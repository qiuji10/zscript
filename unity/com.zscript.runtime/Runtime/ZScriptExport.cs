// ZScriptExport.cs — Attribute definitions for the [ZScriptExport] codegen system.
// Apply [ZScriptExport] to any class, struct, or static class you want accessible
// from ZScript.  Apply [ZScriptHide] to members you want excluded from codegen.
#nullable enable
using System;

namespace ZScript
{
    /// <summary>
    /// Marks a C# type for ZScript binding codegen.
    /// The Unity export generator scans assemblies for this attribute and generates
    /// C# static and/or instance dispatch stubs automatically.
    /// </summary>
    [AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct,
                    AllowMultiple = false, Inherited = false)]
    public sealed class ZScriptExportAttribute : Attribute
    {
        /// <summary>
        /// Optional override for the ZScript-facing identifier.
        /// Defaults to the C# class name if not specified.
        /// </summary>
        public string? Name { get; set; }

        public ZScriptExportAttribute() { }
        public ZScriptExportAttribute(string name) { Name = name; }
    }

    /// <summary>
    /// Excludes a method, property, or field from ZScript binding codegen.
    /// Useful for implementation details that should not be scriptable.
    /// </summary>
    [AttributeUsage(
        AttributeTargets.Method | AttributeTargets.Property | AttributeTargets.Field,
        AllowMultiple = false, Inherited = false)]
    public sealed class ZScriptHideAttribute : Attribute { }
}
