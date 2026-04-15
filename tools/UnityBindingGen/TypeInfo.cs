// TypeInfo.cs — Intermediate representation for a Unity type discovered by
// the scanner. Populated by AssemblyScanner, consumed by code generators.
namespace UnityBindingGen;

public enum MemberKind { Method, Property, Field, StaticMethod, StaticProperty, StaticField }
public enum TypeKind   { MonoBehaviour, PureCSharp, Static, Struct }

public sealed record MemberInfo(
    string     CsName,      // exact C# identifier (no transformation)
    MemberKind Kind,
    string     ReturnType,  // C# return type name (for comment/doc)
    string[]   ParamTypes,  // C# parameter type names
    bool       IsGeneric    // e.g. GetComponent<T>()
);

public sealed record TypeInfo(
    string       CsName,      // exact C# class name
    string       Namespace,
    TypeKind     Kind,
    MemberInfo[] Members
)
{
    // Sanitised name for use as C++ identifier / ZScript global name.
    public string SafeName => CsName.Replace('.', '_');
}
