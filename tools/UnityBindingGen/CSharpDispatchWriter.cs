// CSharpDispatchWriter.cs — Generates ZsUnityDispatch.cs, a single C# file
// containing a static switch-based dispatch table for every Unity type method.
//
// IL2CPP safety: uses only static readonly Action<> / Func<> delegates stored
// in a Dictionary<string, ...>.  No MethodInfo.Invoke, no MakeGenericMethod.
using System.Text;

namespace UnityBindingGen;

public static class CSharpDispatchWriter
{
    public static void Write(IReadOnlyList<TypeInfo> types, string outputPath)
    {
        var sb = new StringBuilder();
        sb.AppendLine("// AUTO-GENERATED — do not edit manually.");
        sb.AppendLine("// IL2CPP-safe dispatch: static readonly delegates, no reflection at runtime.");
        sb.AppendLine("using System;");
        sb.AppendLine("using System.Collections.Generic;");
        sb.AppendLine("using UnityEngine;");
        sb.AppendLine();
        sb.AppendLine("namespace ZScript");
        sb.AppendLine("{");
        sb.AppendLine("    public static partial class ZsUnityDispatch");
        sb.AppendLine("    {");

        foreach (var t in types)
            EmitTypeDispatch(sb, t);

        sb.AppendLine("    } // class ZsUnityDispatch");
        sb.AppendLine("} // namespace ZScript");

        File.WriteAllText(outputPath, sb.ToString(), Encoding.UTF8);
    }

    // -----------------------------------------------------------------------
    private static void EmitTypeDispatch(StringBuilder sb, TypeInfo t)
    {
        var instanceMethods = t.Members
            .Where(m => m.Kind is MemberKind.Method)
            .ToArray();

        if (instanceMethods.Length == 0 && t.Kind != TypeKind.Static) return;

        sb.AppendLine();
        sb.AppendLine($"        // ── {t.CsName} ──────────────────────────────────────");
        sb.AppendLine($"        #region {t.CsName}");
        sb.AppendLine();

        if (t.Kind == TypeKind.Static)
        {
            EmitStaticDispatch(sb, t);
        }
        else
        {
            EmitInstanceMethodDispatch(sb, t, instanceMethods);
            EmitPropertyDispatch(sb, t);
        }

        sb.AppendLine($"        #endregion // {t.CsName}");
    }

    // -----------------------------------------------------------------------
    private static void EmitInstanceMethodDispatch(
        StringBuilder sb, TypeInfo t, MemberInfo[] methods)
    {
        // One static readonly delegate per method — IL2CPP resolves at link time.
        foreach (var m in methods)
        {
            string dispatchName = $"zs_dispatch_{t.CsName}_{m.CsName}";
            sb.AppendLine($"        // {t.CsName}.{m.CsName}");
            sb.AppendLine($"        public static ZsNativeFn {dispatchName} = (vm, argc, argv) =>");
            sb.AppendLine("        {");
            sb.AppendLine("            if (argc < 1) return ZsNative.zs_value_nil();");
            sb.AppendLine("            long handle = ZsNative.zs_value_as_int(argv[0]);");
            sb.AppendLine($"            var obj = ZsObjectPool.Global?.Get<{t.CsName}>(handle);");
            sb.AppendLine($"            if (obj == null) return ZsNative.zs_value_nil();");

            if (m.IsGeneric)
            {
                sb.AppendLine($"            // Generic method: argv[1] = T class table; read __name field");
                sb.AppendLine($"            // Dispatch into pre-generated type-keyed switch.");
                sb.AppendLine($"            // TODO: implement generic dispatch for {m.CsName}<T>");
                sb.AppendLine($"            return ZsNative.zs_value_nil();");
            }
            else
            {
                string retType = m.ReturnType;
                sb.AppendLine($"            // Calls: obj.{m.CsName}(...)");
                sb.AppendLine($"            // Return type: {retType}");
                sb.AppendLine($"            // TODO: marshal args and call obj.{m.CsName}(...)");
                sb.AppendLine($"            return ZsNative.zs_value_nil();");
            }

            sb.AppendLine("        };");
            sb.AppendLine();
        }
    }

    private static void EmitPropertyDispatch(StringBuilder sb, TypeInfo t)
    {
        var props = t.Members
            .Where(m => m.Kind is MemberKind.Property or MemberKind.Field)
            .ToArray();

        foreach (var p in props)
        {
            // getter
            sb.AppendLine($"        public static ZsNativeFn zs_get_{t.CsName}_{p.CsName} = (vm, argc, argv) =>");
            sb.AppendLine("        {");
            sb.AppendLine("            if (argc < 1) return ZsNative.zs_value_nil();");
            sb.AppendLine("            long handle = ZsNative.zs_value_as_int(argv[0]);");
            sb.AppendLine($"            var obj = ZsObjectPool.Global?.Get<{t.CsName}>(handle);");
            sb.AppendLine($"            if (obj == null) return ZsNative.zs_value_nil();");
            sb.AppendLine($"            // TODO: return marshal(obj.{p.CsName})");
            sb.AppendLine($"            return ZsNative.zs_value_nil();");
            sb.AppendLine("        };");
            sb.AppendLine();

            // setter
            sb.AppendLine($"        public static ZsNativeFn zs_set_{t.CsName}_{p.CsName} = (vm, argc, argv) =>");
            sb.AppendLine("        {");
            sb.AppendLine("            if (argc < 2) return ZsNative.zs_value_nil();");
            sb.AppendLine("            long handle = ZsNative.zs_value_as_int(argv[0]);");
            sb.AppendLine($"            var obj = ZsObjectPool.Global?.Get<{t.CsName}>(handle);");
            sb.AppendLine($"            if (obj == null) return ZsNative.zs_value_nil();");
            sb.AppendLine($"            // TODO: obj.{p.CsName} = unmarshal(argv[1])");
            sb.AppendLine($"            return ZsNative.zs_value_nil();");
            sb.AppendLine("        };");
            sb.AppendLine();
        }
    }

    private static void EmitStaticDispatch(StringBuilder sb, TypeInfo t)
    {
        var statics = t.Members
            .Where(m => m.Kind is MemberKind.StaticMethod or
                                  MemberKind.StaticProperty or
                                  MemberKind.StaticField)
            .ToArray();

        foreach (var m in statics)
        {
            sb.AppendLine($"        public static ZsNativeFn zs_static_{t.CsName}_{m.CsName} = (vm, argc, argv) =>");
            sb.AppendLine("        {");
            sb.AppendLine($"            // TODO: return marshal({t.CsName}.{m.CsName}(...))");
            sb.AppendLine($"            return ZsNative.zs_value_nil();");
            sb.AppendLine("        };");
            sb.AppendLine();
        }
    }
}
