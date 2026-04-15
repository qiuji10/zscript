// ExportScanner.cs — Scans assemblies for [ZScriptExport] attributed types
// and builds ExportedTypeInfo records for the code generators.
using System.Reflection;

namespace ZScriptExportGen;

// ── Attribute stubs (defined here so the scanner compiles without UnityEngine) ──
// The real attributes live in unity/Runtime/ZScriptExport.cs
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
public sealed class ZScriptExportAttribute : Attribute
{
    public string? Name { get; set; }
}

[AttributeUsage(AttributeTargets.Method | AttributeTargets.Property | AttributeTargets.Field)]
public sealed class ZScriptHideAttribute : Attribute { }

// ── Intermediate representation ───────────────────────────────────────────────
public enum ExportedMemberKind { Method, Property, Field, GenericMethod }
public enum ExportedTypeKind   { MonoBehaviour, PureCSharp, Static, Struct }

public sealed record ExportedMemberInfo(
    string             ZsName,      // ZScript-facing name (may differ via Name= override)
    string             CsName,      // exact C# identifier
    ExportedMemberKind Kind,
    string             ReturnType,
    string[]           ParamNames,
    string[]           ParamTypes,
    bool               IsGeneric
);

public sealed record ExportedTypeInfo(
    string               ZsName,    // ZScript-facing name (from Name= or class name)
    string               CsName,
    string               Namespace,
    ExportedTypeKind     Kind,
    ExportedMemberInfo[] Members
)
{
    public string SafeName => ZsName.Replace('.', '_').Replace('<', '_').Replace('>', '_');
}

// ── Scanner ───────────────────────────────────────────────────────────────────
public static class ExportScanner
{
    public static List<ExportedTypeInfo> Scan(IEnumerable<string> assemblyPaths)
    {
        var results = new List<ExportedTypeInfo>();

        foreach (string path in assemblyPaths)
        {
            Assembly asm;
            try { asm = Assembly.LoadFrom(path); }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[warn] {path}: {ex.Message}");
                continue;
            }

            foreach (Type t in asm.GetExportedTypes())
            {
                var attr = t.GetCustomAttribute<ZScriptExportAttribute>();
                if (attr == null) continue;
                results.Add(BuildTypeInfo(t, attr));
            }
        }

        return results;
    }

    // -----------------------------------------------------------------------
    private static ExportedTypeInfo BuildTypeInfo(Type t, ZScriptExportAttribute attr)
    {
        string zsName = string.IsNullOrEmpty(attr.Name) ? t.Name : attr.Name;
        var members   = new List<ExportedMemberInfo>();

        const BindingFlags pub = BindingFlags.Public | BindingFlags.Instance |
                                 BindingFlags.Static | BindingFlags.DeclaredOnly;

        foreach (MethodInfo m in t.GetMethods(pub))
        {
            if (m.IsSpecialName) continue;
            if (m.GetCustomAttribute<ZScriptHideAttribute>() != null) continue;

            bool isStatic  = m.IsStatic;
            bool isGeneric = m.IsGenericMethodDefinition;
            var  ps        = m.GetParameters();
            members.Add(new ExportedMemberInfo(
                m.Name, m.Name,
                isGeneric
                    ? ExportedMemberKind.GenericMethod
                    : (isStatic ? ExportedMemberKind.Method : ExportedMemberKind.Method),
                m.ReturnType.Name,
                Array.ConvertAll(ps, p => p.Name ?? "_"),
                Array.ConvertAll(ps, p => p.ParameterType.Name),
                isGeneric));
        }

        foreach (PropertyInfo p in t.GetProperties(pub))
        {
            if (p.GetCustomAttribute<ZScriptHideAttribute>() != null) continue;
            members.Add(new ExportedMemberInfo(
                p.Name, p.Name, ExportedMemberKind.Property,
                p.PropertyType.Name,
                Array.Empty<string>(), Array.Empty<string>(), false));
        }

        foreach (FieldInfo f in t.GetFields(pub))
        {
            if (f.Name.StartsWith('<')) continue;
            if (f.GetCustomAttribute<ZScriptHideAttribute>() != null) continue;
            members.Add(new ExportedMemberInfo(
                f.Name, f.Name, ExportedMemberKind.Field,
                f.FieldType.Name,
                Array.Empty<string>(), Array.Empty<string>(), false));
        }

        return new ExportedTypeInfo(
            zsName, t.Name, t.Namespace ?? "",
            ClassifyType(t),
            members.ToArray());
    }

    private static ExportedTypeKind ClassifyType(Type t)
    {
        for (Type? b = t.BaseType; b != null; b = b.BaseType)
            if (b.Name == "MonoBehaviour") return ExportedTypeKind.MonoBehaviour;
        if (t.IsValueType)  return ExportedTypeKind.Struct;
        if (t.IsAbstract && t.IsSealed) return ExportedTypeKind.Static;
        return ExportedTypeKind.PureCSharp;
    }
}
