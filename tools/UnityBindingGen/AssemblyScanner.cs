// AssemblyScanner.cs — Scans a set of Unity managed DLLs using
// System.Reflection (no Roslyn needed for scanning — Roslyn is only used for
// generating C# output).  Called with the path to the Unity installation's
// Managed/ folder (e.g. <UnityInstall>/Editor/Data/Managed/UnityEngine/).
//
// IL2CPP safety rule: only pre-generated static dispatch is emitted.
// This scanner produces TypeInfo records; generators emit the static tables.
using System.Reflection;

namespace UnityBindingGen;

public static class AssemblyScanner
{
    // Types that are always excluded (internal Unity implementation details).
    private static readonly HashSet<string> ExcludedTypes = new(StringComparer.Ordinal)
    {
        "UnityEngine.Object", // base — bindings handle it implicitly
    };

    // The priority type list from the TODO (exact Unity names).
    public static readonly string[] PriorityTypes =
    {
        "Transform", "GameObject", "Rigidbody", "Rigidbody2D",
        "Collider", "Collider2D", "Camera", "Light",
        "AudioSource", "Animator", "NavMeshAgent", "ParticleSystem",
        "Canvas", "RectTransform", "Image", "Text", "Button",
        // Struct types (marshalled by value)
        "Vector2", "Vector3", "Vector4", "Quaternion",
        "Color", "Rect", "Bounds",
    };

    /// <summary>
    /// Scan the assemblies at the given paths and return one TypeInfo per
    /// discovered public type.  Pass <paramref name="filterNames"/> to limit
    /// output to the priority list; pass null to scan everything.
    /// </summary>
    public static List<TypeInfo> Scan(
        IEnumerable<string> assemblyPaths,
        IEnumerable<string>? filterNames = null)
    {
        var filter = filterNames == null
            ? null
            : new HashSet<string>(filterNames, StringComparer.Ordinal);

        var results = new List<TypeInfo>();

        foreach (string path in assemblyPaths)
        {
            Assembly asm;
            try
            {
                asm = Assembly.LoadFrom(path);
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[warn] could not load {path}: {ex.Message}");
                continue;
            }

            foreach (Type t in asm.GetExportedTypes())
            {
                if (ExcludedTypes.Contains(t.FullName ?? "")) continue;
                if (filter != null && !filter.Contains(t.Name)) continue;

                results.Add(BuildTypeInfo(t));
            }
        }

        return results;
    }

    // -----------------------------------------------------------------------
    private static TypeInfo BuildTypeInfo(Type t)
    {
        TypeKind kind = ClassifyType(t);
        var members   = new List<MemberInfo>();

        const BindingFlags pub = BindingFlags.Public | BindingFlags.Instance |
                                 BindingFlags.Static | BindingFlags.DeclaredOnly;

        // Methods
        foreach (MethodInfo m in t.GetMethods(pub))
        {
            if (m.IsSpecialName) continue;  // skip property get_/set_ accessors
            if (m.Name.StartsWith("get_") || m.Name.StartsWith("set_")) continue;

            bool isStatic  = m.IsStatic;
            bool isGeneric = m.IsGenericMethodDefinition;
            members.Add(new MemberInfo(
                m.Name,
                isStatic ? MemberKind.StaticMethod : MemberKind.Method,
                m.ReturnType.Name,
                Array.ConvertAll(m.GetParameters(), p => p.ParameterType.Name),
                isGeneric));
        }

        // Properties
        foreach (PropertyInfo p in t.GetProperties(pub))
        {
            bool isStatic = (p.GetMethod?.IsStatic ?? p.SetMethod?.IsStatic) == true;
            if (p.GetMethod != null)
                members.Add(new MemberInfo(p.Name,
                    isStatic ? MemberKind.StaticProperty : MemberKind.Property,
                    p.PropertyType.Name, Array.Empty<string>(), false));
        }

        // Public fields
        foreach (FieldInfo f in t.GetFields(pub))
        {
            if (f.Name.StartsWith('<')) continue; // compiler-generated backing fields
            members.Add(new MemberInfo(f.Name,
                f.IsStatic ? MemberKind.StaticField : MemberKind.Field,
                f.FieldType.Name, Array.Empty<string>(), false));
        }

        return new TypeInfo(t.Name, t.Namespace ?? "", kind, members.ToArray());
    }

    private static TypeKind ClassifyType(Type t)
    {
        // Walk the base chain looking for known Unity base types.
        for (Type? b = t.BaseType; b != null; b = b.BaseType)
        {
            if (b.FullName == "UnityEngine.MonoBehaviour") return TypeKind.MonoBehaviour;
        }
        if (t.IsValueType)  return TypeKind.Struct;
        if (t.IsAbstract && t.IsSealed) return TypeKind.Static; // static class in IL
        return TypeKind.PureCSharp;
    }
}
