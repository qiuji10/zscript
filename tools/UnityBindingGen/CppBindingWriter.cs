// CppBindingWriter.cs — Generates one .cpp file per Unity type containing a
// register_<TypeName>(VM& vm) function that builds the ZScript class table.
//
// IL2CPP safety: all dispatch goes through pre-generated static dictionaries.
// No MakeGenericMethod or Activator.CreateInstance at runtime.
using System.Text;

namespace UnityBindingGen;

public static class CppBindingWriter
{
    /// <summary>
    /// Write one binding .cpp file for each TypeInfo into outputDir.
    /// Also writes a register_unity_builtins.cpp bulk entry point.
    /// </summary>
    public static void WriteAll(IReadOnlyList<TypeInfo> types, string outputDir)
    {
        Directory.CreateDirectory(outputDir);

        foreach (var t in types)
            WriteType(t, outputDir);

        WriteBulkEntryPoint(types, outputDir);
    }

    // -----------------------------------------------------------------------
    private static void WriteType(TypeInfo t, string outputDir)
    {
        var sb = new StringBuilder();
        string fn = $"register_{t.SafeName}";

        sb.AppendLine("// AUTO-GENERATED — do not edit manually.");
        sb.AppendLine($"// Source: UnityBindingGen for {t.CsName}");
        sb.AppendLine("#include \"vm.h\"");
        sb.AppendLine("#include \"unity/zscript_c.h\"");
        sb.AppendLine("#include <string>");
        sb.AppendLine();
        sb.AppendLine("namespace zscript_unity {");
        sb.AppendLine();

        // ── dispatch helpers ────────────────────────────────────────────────
        EmitDispatchHelpers(sb, t);

        // ── register_<Name> ─────────────────────────────────────────────────
        sb.AppendLine($"void {fn}(zscript::VM& vm) {{");
        sb.AppendLine($"    // ZScript class table for {t.CsName}");
        sb.AppendLine($"    zscript::Value cls = zscript::Value::from_table();");
        sb.AppendLine($"    auto* tbl = cls.as_table();");
        sb.AppendLine($"    tbl->set(\"__name\", zscript::Value::from_string(\"{t.CsName}\"));");
        sb.AppendLine();

        if (t.Kind == TypeKind.Static)
        {
            // Static class: methods and properties go directly on the table.
            EmitStaticMembers(sb, t);
        }
        else
        {
            // Instance type: __index / __newindex dispatch, plus __call if constructable.
            EmitIndexMetamethod(sb, t);
            EmitNewIndexMetamethod(sb, t);
            if (t.Kind == TypeKind.PureCSharp || t.Kind == TypeKind.Struct)
                EmitCallMetamethod(sb, t);
        }

        sb.AppendLine($"    vm.set_global(\"{t.CsName}\", cls);");
        sb.AppendLine("}");
        sb.AppendLine();
        sb.AppendLine("} // namespace zscript_unity");

        string outPath = Path.Combine(outputDir, $"{t.SafeName}_binding.cpp");
        File.WriteAllText(outPath, sb.ToString(), Encoding.UTF8);
    }

    // -----------------------------------------------------------------------
    // Emit a static dispatch table for instance methods.
    // Each entry is a lambda that extracts (handle, args) and calls through
    // the C API dispatch function (ZsUnityDispatch.cs on the C# side).
    // -----------------------------------------------------------------------
    private static void EmitDispatchHelpers(StringBuilder sb, TypeInfo t)
    {
        var instanceMethods = t.Members
            .Where(m => m.Kind is MemberKind.Method)
            .ToArray();

        if (instanceMethods.Length == 0) return;

        sb.AppendLine("// Forward declaration — implemented in ZsUnityDispatch.cs (C# side).");
        sb.AppendLine("// The C API dispatch bridge calls this via a registered native.");
        sb.AppendLine($"// static dispatch table for {t.CsName}::methods");
        sb.AppendLine($"static const std::unordered_map<std::string,");
        sb.AppendLine($"    std::function<zscript::Value(zscript::VM&, int64_t, std::vector<zscript::Value>)>>");
        sb.AppendLine($"    k_{t.SafeName}_methods = {{");

        foreach (var m in instanceMethods)
        {
            string escapedName = m.CsName.Replace("\"", "\\\"");
            sb.AppendLine($"    {{\"{escapedName}\", [](zscript::VM& vm, int64_t handle, std::vector<zscript::Value> args) {{");
            sb.AppendLine($"        // Dispatch to C# side via registered native 'zs_dispatch_{t.CsName}_{m.CsName}'");
            sb.AppendLine($"        auto fn = vm.get_global(\"zs_dispatch_{t.CsName}_{m.CsName}\");");
            sb.AppendLine($"        if (fn.is_native() || fn.is_closure()) {{");
            sb.AppendLine($"            std::vector<zscript::Value> call_args = {{zscript::Value::from_int(handle)}};");
            sb.AppendLine($"            call_args.insert(call_args.end(), args.begin(), args.end());");
            sb.AppendLine($"            return vm.call_value(fn, std::move(call_args));");
            sb.AppendLine($"        }}");
            sb.AppendLine($"        return zscript::Value::nil();");
            sb.AppendLine($"    }}}}," );
        }

        sb.AppendLine("};");
        sb.AppendLine();
    }

    private static void EmitIndexMetamethod(StringBuilder sb, TypeInfo t)
    {
        var readable = t.Members
            .Where(m => m.Kind is MemberKind.Property or MemberKind.Field)
            .ToArray();

        sb.AppendLine("    // __index: property reads + method lookups");
        sb.AppendLine("    tbl->set(\"__index\", zscript::Value::from_native(\"__index\",");
        sb.AppendLine($"        [](std::vector<zscript::Value> args) -> std::vector<zscript::Value> {{");
        sb.AppendLine("        if (args.size() < 2 || !args[0].is_table() || !args[1].is_string())");
        sb.AppendLine("            return {zscript::Value::nil()};");
        sb.AppendLine("        int64_t handle = args[0].as_table()->get(\"__handle\").as_int();");
        sb.AppendLine("        const std::string& key = args[1].as_string();");
        sb.AppendLine();

        if (readable.Length > 0)
        {
            sb.AppendLine("        // Property / field reads");
            foreach (var m in readable)
            {
                string escaped = m.CsName.Replace("\"", "\\\"");
                sb.AppendLine($"        if (key == \"{escaped}\") {{");
                sb.AppendLine($"            // C# side: ObjectPool.Get(handle).{m.CsName}");
                sb.AppendLine($"            // TODO: call registered native zs_get_{t.CsName}_{m.CsName}(handle)");
                sb.AppendLine($"            return {{zscript::Value::nil()}}; // placeholder");
                sb.AppendLine($"        }}");
            }
            sb.AppendLine();
        }

        // Method lookup: return a bound native.
        sb.AppendLine($"        // Method lookup");
        sb.AppendLine($"        auto it = k_{t.SafeName}_methods.find(key);");
        sb.AppendLine($"        if (it != k_{t.SafeName}_methods.end()) {{");
        sb.AppendLine($"            auto method_fn = it->second;");
        sb.AppendLine($"            return {{zscript::Value::from_native(key,");
        sb.AppendLine($"                [handle, method_fn](std::vector<zscript::Value> margs) -> std::vector<zscript::Value> {{");
        sb.AppendLine($"                    return {{method_fn(*(zscript::VM*)nullptr, handle, margs)}};");
        sb.AppendLine($"                }})");
        sb.AppendLine($"            }};");
        sb.AppendLine($"        }}");
        sb.AppendLine("        return {zscript::Value::nil()};");
        sb.AppendLine("    }));");
        sb.AppendLine();
    }

    private static void EmitNewIndexMetamethod(StringBuilder sb, TypeInfo t)
    {
        var writable = t.Members
            .Where(m => m.Kind is MemberKind.Property or MemberKind.Field)
            .ToArray();

        if (writable.Length == 0) return;

        sb.AppendLine("    // __newindex: property writes");
        sb.AppendLine("    tbl->set(\"__newindex\", zscript::Value::from_native(\"__newindex\",");
        sb.AppendLine($"        [](std::vector<zscript::Value> args) -> std::vector<zscript::Value> {{");
        sb.AppendLine("        if (args.size() < 3 || !args[0].is_table() || !args[1].is_string())");
        sb.AppendLine("            return {};");
        sb.AppendLine("        int64_t handle = args[0].as_table()->get(\"__handle\").as_int();");
        sb.AppendLine("        const std::string& key = args[1].as_string();");
        sb.AppendLine("        // const zscript::Value& val = args[2];");
        sb.AppendLine();

        foreach (var m in writable)
        {
            string escaped = m.CsName.Replace("\"", "\\\"");
            sb.AppendLine($"        if (key == \"{escaped}\") {{");
            sb.AppendLine($"            // TODO: call registered native zs_set_{t.CsName}_{m.CsName}(handle, val)");
            sb.AppendLine($"            return {{}};");
            sb.AppendLine($"        }}");
        }

        sb.AppendLine("        return {};");
        sb.AppendLine("    }));");
        sb.AppendLine();
    }

    private static void EmitCallMetamethod(StringBuilder sb, TypeInfo t)
    {
        sb.AppendLine("    // __call: constructor (pure C# / struct types only)");
        sb.AppendLine("    tbl->set(\"__call\", zscript::Value::from_native(\"__call\",");
        sb.AppendLine($"        [](std::vector<zscript::Value> args) -> std::vector<zscript::Value> {{");
        sb.AppendLine($"        // args[0] = class table (self); args[1..] = ctor params");
        sb.AppendLine($"        // TODO: call registered native zs_new_{t.CsName}(args...)");
        sb.AppendLine($"        return {{zscript::Value::nil()}};");
        sb.AppendLine("    }));");
        sb.AppendLine();
    }

    private static void EmitStaticMembers(StringBuilder sb, TypeInfo t)
    {
        foreach (var m in t.Members.Where(m =>
            m.Kind is MemberKind.StaticMethod or
                      MemberKind.StaticProperty or
                      MemberKind.StaticField))
        {
            sb.AppendLine($"    tbl->set(\"{m.CsName}\", zscript::Value::from_native(\"{m.CsName}\",");
            sb.AppendLine($"        [](std::vector<zscript::Value> args) -> std::vector<zscript::Value> {{");
            sb.AppendLine($"        // TODO: call registered native zs_static_{t.CsName}_{m.CsName}(args...)");
            sb.AppendLine($"        return {{zscript::Value::nil()}};");
            sb.AppendLine($"    }}));");
        }
        sb.AppendLine();
    }

    // -----------------------------------------------------------------------
    private static void WriteBulkEntryPoint(IReadOnlyList<TypeInfo> types, string outputDir)
    {
        var sb = new StringBuilder();
        sb.AppendLine("// AUTO-GENERATED — do not edit manually.");
        sb.AppendLine("// Bulk registration entry point — call this from ZScriptVM at startup.");
        sb.AppendLine("#include \"vm.h\"");
        sb.AppendLine();
        sb.AppendLine("namespace zscript_unity {");
        sb.AppendLine();

        foreach (var t in types)
            sb.AppendLine($"void register_{t.SafeName}(zscript::VM& vm);");

        sb.AppendLine();
        sb.AppendLine("void register_unity_builtins(zscript::VM& vm) {");
        foreach (var t in types)
            sb.AppendLine($"    register_{t.SafeName}(vm);");
        sb.AppendLine("}");
        sb.AppendLine();
        sb.AppendLine("} // namespace zscript_unity");

        File.WriteAllText(
            Path.Combine(outputDir, "register_unity_builtins.cpp"),
            sb.ToString(), Encoding.UTF8);
    }
}
