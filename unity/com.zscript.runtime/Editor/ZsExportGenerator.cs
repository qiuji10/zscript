// ZsExportGenerator.cs — Codegen tool for [ZScriptExport] types.
//
// Run via: Tools > ZScript > Generate Export Bindings
//
// For every type marked [ZScriptExport] the generator creates a C# file under
// Assets/ZScriptGenerated/ that implements IZScriptExport.  ZScriptVM.Awake()
// discovers these classes via reflection and calls Register() automatically,
// so no manual wiring is needed.
//
// Supported member kinds:
//   • public static void/bool/int/long/float/double/string methods
//   • public static properties with get/set of the above types
//   • public static event Action / Action<T> where T is a primitive type
//
// Unsupported members are skipped with a // TODO comment in the generated file.
//
// Event subscription in ZScript (table-based API):
//   MyManager.onScoreChanged.AddListener(fn(score) { ... })
//   id = MyManager.onScoreChanged.AddListener(...)
//   MyManager.onScoreChanged.RemoveListener(id)
//   MyManager.onScoreChanged.RemoveAllListeners()
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using UnityEditor;
using UnityEngine;

namespace ZScript.Editor
{
    public static class ZsExportGenerator
    {
        private const string OutputDir = "Assets/ZScriptGenerated";

        // ----------------------------------------------------------------
        // Menu entry point
        // ----------------------------------------------------------------

        [MenuItem("Tools/ZScript/Generate Export Bindings")]
        public static void GenerateAll()
        {
            var types = FindExportTypes();
            if (types.Count == 0)
            {
                EditorUtility.DisplayDialog(
                    "ZScript Export Generator",
                    "No types marked with [ZScriptExport] were found in any user assembly.\n\n" +
                    "Apply [ZScriptExport] to a public class or static class you want " +
                    "to expose to ZScript, then run this tool again.",
                    "OK");
                return;
            }

            Directory.CreateDirectory(OutputDir);

            int generated = 0;
            var generatedTypes = new List<Type>();

            foreach (var type in types)
            {
                string code = GenerateBinding(type);
                if (code == null) continue;

                string path = Path.Combine(OutputDir, $"{type.Name}_ZsBinding.cs");
                File.WriteAllText(path, code, Encoding.UTF8);
                generatedTypes.Add(type);
                generated++;
            }

            // Write a helper file with the ReadString utility used by generated bindings.
            File.WriteAllText(
                Path.Combine(OutputDir, "ZsBindingHelper.cs"),
                GenerateHelperFile(),
                Encoding.UTF8);

            AssetDatabase.Refresh();
            Debug.Log($"[ZScript] Export Generator: wrote {generated} binding(s) to {OutputDir}/");
        }

        [MenuItem("Tools/ZScript/Clear Generated Bindings")]
        public static void ClearGenerated()
        {
            if (!Directory.Exists(OutputDir)) return;
            if (!EditorUtility.DisplayDialog(
                    "Clear Generated Bindings",
                    $"Delete all files in {OutputDir}/?",
                    "Delete", "Cancel"))
                return;

            Directory.Delete(OutputDir, recursive: true);
            string meta = OutputDir + ".meta";
            if (File.Exists(meta)) File.Delete(meta);
            AssetDatabase.Refresh();
            Debug.Log("[ZScript] Cleared generated bindings.");
        }

        // ----------------------------------------------------------------
        // Discovery
        // ----------------------------------------------------------------

        private static List<Type> FindExportTypes()
        {
            var result = new List<Type>();
            foreach (var asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                string name = asm.GetName().Name;
                if (name.StartsWith("UnityEngine",  StringComparison.Ordinal) ||
                    name.StartsWith("UnityEditor",  StringComparison.Ordinal) ||
                    name.StartsWith("Unity.",       StringComparison.Ordinal) ||
                    name.StartsWith("System",       StringComparison.Ordinal) ||
                    name.StartsWith("mscorlib",     StringComparison.Ordinal) ||
                    name.StartsWith("Mono.",        StringComparison.Ordinal) ||
                    name.StartsWith("nunit",        StringComparison.Ordinal))
                    continue;
                try
                {
                    foreach (var t in asm.GetTypes())
                    {
                        if (t.GetCustomAttribute<ZScriptExportAttribute>() != null)
                            result.Add(t);
                    }
                }
                catch { /* reflection-only or access-restricted assembly */ }
            }
            return result;
        }

        // ----------------------------------------------------------------
        // Code generation
        // ----------------------------------------------------------------

        private static string GenerateBinding(Type type)
        {
            var attr   = type.GetCustomAttribute<ZScriptExportAttribute>();
            string zsName = !string.IsNullOrEmpty(attr?.Name) ? attr.Name : type.Name;

            var methods    = GetExportMethods(type);
            var properties = GetExportProperties(type);
            var events     = GetExportEvents(type);

            var sb = new StringBuilder();
            sb.AppendLine("// Auto-generated by ZScript Export Generator.");
            sb.AppendLine("// Re-run Tools > ZScript > Generate Export Bindings to update.");
            sb.AppendLine("// Do NOT edit this file manually — changes will be overwritten.");
            sb.AppendLine($"// Source type: {type.FullName}");
            sb.AppendLine("using System;");
            sb.AppendLine("using System.Collections.Generic;");
            sb.AppendLine("using ZScript;");
            sb.AppendLine("using ZScriptGenerated;");
            sb.AppendLine();
            sb.AppendLine("namespace ZScriptGenerated");
            sb.AppendLine("{");
            sb.AppendLine($"    [ZScriptGeneratedBinding]");
            sb.AppendLine($"    public sealed class {type.Name}_ZsBinding : IZScriptExport");
            sb.AppendLine("    {");

            // Delegate-holding fields prevent GC from collecting the delegates while
            // the VM is alive, since only C++ holds a raw function pointer to them.
            sb.AppendLine("        // Held to prevent delegate GC while the VM is alive.");
            sb.AppendLine("        private readonly List<ZsNativeFn> _delegates = new List<ZsNativeFn>();");
            sb.AppendLine();

            // ---- Register() ----
            sb.AppendLine("        public void Register(ZScriptVM vm)");
            sb.AppendLine("        {");
            sb.AppendLine("            var tbl = new ZsValueHandle(ZsNative.zs_table_new());");
            sb.AppendLine();

            // Methods
            foreach (var m in methods)
                EmitMethod(sb, type, m);

            // Properties
            foreach (var p in properties)
                EmitProperty(sb, type, p);

            // Events
            foreach (var ev in events)
                EmitEvent(sb, type, ev);

            // Unsupported skipped — emit TODO comments
            EmitSkippedMembers(sb, type, methods, properties, events);

            sb.AppendLine($"            ZsNative.zs_vm_set_global(vm.RawVM, \"{zsName}\", tbl.Raw);");
            sb.AppendLine("            tbl.Dispose();");
            sb.AppendLine("        }");
            sb.AppendLine("    }");
            sb.AppendLine("}");

            return sb.ToString();
        }

        // ----------------------------------------------------------------
        // Member collection
        // ----------------------------------------------------------------

        private static List<MethodInfo> GetExportMethods(Type type)
        {
            var result = new List<MethodInfo>();
            foreach (var m in type.GetMethods(BindingFlags.Public | BindingFlags.Static))
            {
                if (m.IsSpecialName) continue; // skip property getters/setters
                if (m.GetCustomAttribute<ZScriptHideAttribute>() != null) continue;
                if (m.DeclaringType == typeof(object)) continue;
                if (!IsSupportedReturnType(m.ReturnType)) continue;
                if (!m.GetParameters().All(p => IsSupportedParamType(p.ParameterType))) continue;
                result.Add(m);
            }
            return result;
        }

        private static List<PropertyInfo> GetExportProperties(Type type)
        {
            var result = new List<PropertyInfo>();
            foreach (var p in type.GetProperties(BindingFlags.Public | BindingFlags.Static))
            {
                if (p.GetCustomAttribute<ZScriptHideAttribute>() != null) continue;
                if (!IsSupportedParamType(p.PropertyType)) continue;
                result.Add(p);
            }
            return result;
        }

        private static List<EventInfo> GetExportEvents(Type type)
        {
            var result = new List<EventInfo>();
            foreach (var ev in type.GetEvents(BindingFlags.Public | BindingFlags.Static))
            {
                if (ev.GetCustomAttribute<ZScriptHideAttribute>() != null) continue;
                if (!IsSupportedEventType(ev.EventHandlerType)) continue;
                result.Add(ev);
            }
            return result;
        }

        // ----------------------------------------------------------------
        // Emitters
        // ----------------------------------------------------------------

        private static void EmitMethod(StringBuilder sb, Type type, MethodInfo m)
        {
            string fnName  = m.Name;
            var    @params = m.GetParameters();
            bool   isVoid  = m.ReturnType == typeof(void);

            sb.AppendLine($"            // {type.Name}.{fnName}");
            sb.AppendLine($"            {{");
            sb.AppendLine($"                ZsNativeFn fn = (_, argc, argv) =>");
            sb.AppendLine($"                {{");

            // Marshal arguments
            for (int i = 0; i < @params.Length; i++)
            {
                var p = @params[i];
                string local = $"arg_{p.Name}";
                sb.AppendLine($"                    {CsTypeName(p.ParameterType)} {local} = " +
                              $"argc > {i} ? {UnmarshalExpr(p.ParameterType, $"argv[{i}]")} : default;");
            }

            // Call
            string argList = string.Join(", ", @params.Select(p => $"arg_{p.Name}"));
            if (isVoid)
            {
                sb.AppendLine($"                    {type.FullName}.{fnName}({argList});");
                sb.AppendLine($"                    return ZsNative.zs_value_nil();");
            }
            else
            {
                sb.AppendLine($"                    var ret = {type.FullName}.{fnName}({argList});");
                sb.AppendLine($"                    return {MarshalExpr(m.ReturnType, "ret")};");
            }

            sb.AppendLine($"                }};");
            sb.AppendLine($"                _delegates.Add(fn);");
            sb.AppendLine($"                ZsNative.zs_table_set_fn(tbl.Raw, \"{fnName}\", fn, vm.RawVM);");
            sb.AppendLine($"            }}");
            sb.AppendLine();
        }

        private static void EmitProperty(StringBuilder sb, Type type, PropertyInfo p)
        {
            string propName = p.Name;

            if (p.CanRead)
            {
                sb.AppendLine($"            // {type.Name}.{propName} getter");
                sb.AppendLine($"            {{");
                sb.AppendLine($"                ZsNativeFn fn = (_, __, ___) =>");
                sb.AppendLine($"                    {MarshalExpr(p.PropertyType, $"{type.FullName}.{propName}")};");
                sb.AppendLine($"                _delegates.Add(fn);");
                sb.AppendLine($"                ZsNative.zs_table_set_fn(tbl.Raw, \"get{propName}\", fn, vm.RawVM);");
                sb.AppendLine($"            }}");
            }

            if (p.CanWrite)
            {
                sb.AppendLine($"            // {type.Name}.{propName} setter");
                sb.AppendLine($"            {{");
                sb.AppendLine($"                ZsNativeFn fn = (_, argc, argv) =>");
                sb.AppendLine($"                {{");
                sb.AppendLine($"                    if (argc >= 1)");
                sb.AppendLine($"                        {type.FullName}.{propName} = " +
                              $"{UnmarshalExpr(p.PropertyType, "argv[0]")};");
                sb.AppendLine($"                    return ZsNative.zs_value_nil();");
                sb.AppendLine($"                }};");
                sb.AppendLine($"                _delegates.Add(fn);");
                sb.AppendLine($"                ZsNative.zs_table_set_fn(tbl.Raw, \"set{propName}\", fn, vm.RawVM);");
                sb.AppendLine($"            }}");
            }

            sb.AppendLine();
        }

        private static void EmitEvent(StringBuilder sb, Type type, EventInfo ev)
        {
            // Determine parameter type (Action has none; Action<T> has one).
            var handlerType = ev.EventHandlerType;
            var typeArgs    = handlerType.IsGenericType
                ? handlerType.GetGenericArguments() : Array.Empty<Type>();

            string evField  = char.ToLowerInvariant(ev.Name[0]) + ev.Name.Substring(1); // camelCase
            string evName   = ev.Name;
            string csType   = typeArgs.Length == 1 ? CsTypeName(typeArgs[0]) : null;

            sb.AppendLine($"            // {type.Name}.{evName} event");
            sb.AppendLine($"            {{");
            sb.AppendLine($"                var listeners = new Dictionary<long, {ActionTypeName(typeArgs)}>();");
            sb.AppendLine($"                long nextId = 1L;");
            sb.AppendLine($"                var evTbl = new ZsValueHandle(ZsNative.zs_table_new());");
            sb.AppendLine();

            // AddListener
            sb.AppendLine($"                ZsNativeFn addListener = (_, argc, argv) =>");
            sb.AppendLine($"                {{");
            sb.AppendLine($"                    if (argc < 1) return ZsNative.zs_value_nil();");
            sb.AppendLine($"                    IntPtr fnRaw = ZsNative.zs_value_clone(argv[0]);");
            sb.AppendLine($"                    long id = nextId++;");
            if (typeArgs.Length == 0)
            {
                sb.AppendLine($"                    {ActionTypeName(typeArgs)} handler = () =>");
                sb.AppendLine($"                    {{");
                sb.AppendLine($"                        ZsNative.zs_value_invoke(vm.RawVM, fnRaw,");
                sb.AppendLine($"                            0, Array.Empty<IntPtr>(), out IntPtr _, new byte[256], 256);");
                sb.AppendLine($"                    }};");
            }
            else
            {
                var tArg = typeArgs[0];
                sb.AppendLine($"                    {ActionTypeName(typeArgs)} handler = ({csType} v) =>");
                sb.AppendLine($"                    {{");
                sb.AppendLine($"                        using var arg = {FromCsExpr(tArg, "v")};");
                sb.AppendLine($"                        ZsNative.zs_value_invoke(vm.RawVM, fnRaw,");
                sb.AppendLine($"                            1, new[] {{ arg.Raw }}, out IntPtr _, new byte[256], 256);");
                sb.AppendLine($"                    }};");
            }
            sb.AppendLine($"                    listeners[id] = handler;");
            sb.AppendLine($"                    {type.FullName}.{evName} += handler;");
            sb.AppendLine($"                    return ZsNative.zs_value_int(id);");
            sb.AppendLine($"                }};");
            sb.AppendLine($"                _delegates.Add(addListener);");
            sb.AppendLine($"                ZsNative.zs_table_set_fn(evTbl.Raw, \"AddListener\", addListener, vm.RawVM);");
            sb.AppendLine();

            // RemoveListener
            sb.AppendLine($"                ZsNativeFn removeListener = (_, argc, argv) =>");
            sb.AppendLine($"                {{");
            sb.AppendLine($"                    if (argc < 1) return ZsNative.zs_value_nil();");
            sb.AppendLine($"                    long id = ZsNative.zs_value_as_int(argv[0]);");
            sb.AppendLine($"                    if (listeners.TryGetValue(id, out var h))");
            sb.AppendLine($"                    {{");
            sb.AppendLine($"                        {type.FullName}.{evName} -= h;");
            sb.AppendLine($"                        listeners.Remove(id);");
            sb.AppendLine($"                    }}");
            sb.AppendLine($"                    return ZsNative.zs_value_nil();");
            sb.AppendLine($"                }};");
            sb.AppendLine($"                _delegates.Add(removeListener);");
            sb.AppendLine($"                ZsNative.zs_table_set_fn(evTbl.Raw, \"RemoveListener\", removeListener, vm.RawVM);");
            sb.AppendLine();

            // RemoveAllListeners
            sb.AppendLine($"                ZsNativeFn removeAll = (_, __, ___) =>");
            sb.AppendLine($"                {{");
            sb.AppendLine($"                    foreach (var h in listeners.Values)");
            sb.AppendLine($"                        {type.FullName}.{evName} -= h;");
            sb.AppendLine($"                    listeners.Clear();");
            sb.AppendLine($"                    return ZsNative.zs_value_nil();");
            sb.AppendLine($"                }};");
            sb.AppendLine($"                _delegates.Add(removeAll);");
            sb.AppendLine($"                ZsNative.zs_table_set_fn(evTbl.Raw, \"RemoveAllListeners\", removeAll, vm.RawVM);");
            sb.AppendLine();

            sb.AppendLine($"                ZsNative.zs_table_set_value(tbl.Raw, \"{evField}\", evTbl.Raw);");
            sb.AppendLine($"                evTbl.Dispose();");
            sb.AppendLine($"            }}");
            sb.AppendLine();
        }

        private static void EmitSkippedMembers(StringBuilder sb, Type type,
            List<MethodInfo> methods, List<PropertyInfo> props, List<EventInfo> evs)
        {
            // Find all public static members we did NOT handle.
            var handledMethods = new HashSet<string>(methods.Select(m => m.Name));
            var handledProps   = new HashSet<string>(props.Select(p => p.Name));
            var handledEvs     = new HashSet<string>(evs.Select(e => e.Name));

            bool any = false;
            foreach (var m in type.GetMethods(BindingFlags.Public | BindingFlags.Static))
            {
                if (m.IsSpecialName || m.DeclaringType == typeof(object)) continue;
                if (m.GetCustomAttribute<ZScriptHideAttribute>() != null) continue;
                if (handledMethods.Contains(m.Name)) continue;
                if (!any) { sb.AppendLine("            // ---- Skipped (unsupported types) ----"); any = true; }
                sb.AppendLine($"            // TODO: {type.Name}.{m.Name} — " +
                              $"return or parameter type not supported by codegen.");
            }
            foreach (var p in type.GetProperties(BindingFlags.Public | BindingFlags.Static))
            {
                if (p.GetCustomAttribute<ZScriptHideAttribute>() != null) continue;
                if (handledProps.Contains(p.Name)) continue;
                if (!any) { sb.AppendLine("            // ---- Skipped (unsupported types) ----"); any = true; }
                sb.AppendLine($"            // TODO: {type.Name}.{p.Name} — " +
                              $"property type '{p.PropertyType.Name}' not supported by codegen.");
            }
            foreach (var e in type.GetEvents(BindingFlags.Public | BindingFlags.Static))
            {
                if (e.GetCustomAttribute<ZScriptHideAttribute>() != null) continue;
                if (handledEvs.Contains(e.Name)) continue;
                if (!any) { sb.AppendLine("            // ---- Skipped (unsupported types) ----"); any = true; }
                sb.AppendLine($"            // TODO: {type.Name}.{e.Name} — " +
                              $"event handler type '{e.EventHandlerType.Name}' not supported by codegen.");
            }
            if (any) sb.AppendLine();
        }

        // ----------------------------------------------------------------
        // Type support predicates
        // ----------------------------------------------------------------

        private static readonly HashSet<Type> s_primitives = new HashSet<Type>
        {
            typeof(bool), typeof(int), typeof(long),
            typeof(float), typeof(double), typeof(string),
        };

        private static bool IsSupportedReturnType(Type t)
            => t == typeof(void) || s_primitives.Contains(t);

        private static bool IsSupportedParamType(Type t)
            => s_primitives.Contains(t);

        private static bool IsSupportedEventType(Type t)
        {
            if (t == typeof(Action)) return true;
            if (!t.IsGenericType) return false;
            var def  = t.GetGenericTypeDefinition();
            if (def != typeof(Action<>)) return false;
            return s_primitives.Contains(t.GetGenericArguments()[0]);
        }

        // ----------------------------------------------------------------
        // Code helpers
        // ----------------------------------------------------------------

        private static string CsTypeName(Type t)
        {
            if (t == typeof(bool))   return "bool";
            if (t == typeof(int))    return "int";
            if (t == typeof(long))   return "long";
            if (t == typeof(float))  return "float";
            if (t == typeof(double)) return "double";
            if (t == typeof(string)) return "string";
            return t.FullName;
        }

        private static string ActionTypeName(Type[] typeArgs)
        {
            if (typeArgs.Length == 0) return "Action";
            return $"Action<{CsTypeName(typeArgs[0])}>";
        }

        // C# value → ZsValue (owned, caller must free)
        private static string MarshalExpr(Type t, string expr)
        {
            if (t == typeof(bool))   return $"ZsNative.zs_value_bool({expr} ? 1 : 0)";
            if (t == typeof(int))    return $"ZsNative.zs_value_int({expr})";
            if (t == typeof(long))   return $"ZsNative.zs_value_int({expr})";
            if (t == typeof(float))  return $"ZsNative.zs_value_float({expr})";
            if (t == typeof(double)) return $"ZsNative.zs_value_float({expr})";
            if (t == typeof(string)) return $"ZsNative.zs_value_string({expr} ?? string.Empty)";
            return $"ZsNative.zs_value_nil() /* unsupported: {t.Name} */";
        }

        // ZsValue (raw IntPtr, NOT owned) → C# value
        private static string UnmarshalExpr(Type t, string rawExpr)
        {
            if (t == typeof(bool))   return $"ZsNative.zs_value_as_bool({rawExpr}) != 0";
            if (t == typeof(int))    return $"(int)ZsNative.zs_value_as_int({rawExpr})";
            if (t == typeof(long))   return $"ZsNative.zs_value_as_int({rawExpr})";
            if (t == typeof(float))  return $"(float)ZsNative.zs_value_as_float({rawExpr})";
            if (t == typeof(double)) return $"ZsNative.zs_value_as_float({rawExpr})";
            if (t == typeof(string)) return $"ZsBindingHelper.ReadString({rawExpr})";
            return $"default /* unsupported: {t.Name} */";
        }

        // C# value → owned ZsValueHandle (for event argument passing)
        private static string FromCsExpr(Type t, string expr)
        {
            if (t == typeof(bool))   return $"ZsValueHandle.FromBool({expr})";
            if (t == typeof(int))    return $"ZsValueHandle.FromInt({expr})";
            if (t == typeof(long))   return $"ZsValueHandle.FromInt({expr})";
            if (t == typeof(float))  return $"ZsValueHandle.FromFloat({expr})";
            if (t == typeof(double)) return $"ZsValueHandle.FromFloat({expr})";
            if (t == typeof(string)) return $"ZsValueHandle.FromString({expr} ?? string.Empty)";
            return $"ZsValueHandle.Nil() /* unsupported: {t.Name} */";
        }

        // ----------------------------------------------------------------
        // Helper file
        // ----------------------------------------------------------------

        private static string GenerateHelperFile() => @"// Auto-generated — do not edit.
// Shared helper utilities used by all ZScript generated binding files.
using System.Text;
using ZScript;

namespace ZScriptGenerated
{
    internal static class ZsBindingHelper
    {
        /// <summary>
        /// Read a ZScript string value from a raw IntPtr without taking ownership.
        /// Uses a 4096-byte initial buffer and retries with exact size if truncated.
        /// </summary>
        internal static string ReadString(System.IntPtr val)
        {
            byte[] buf = new byte[4096];
            int len = ZsNative.zs_value_as_string(val, buf, buf.Length);
            if (len <= 0) return string.Empty;
            if (len >= buf.Length)
            {
                buf = new byte[len + 1];
                ZsNative.zs_value_as_string(val, buf, buf.Length);
            }
            return Encoding.UTF8.GetString(buf, 0, System.Math.Min(len, buf.Length - 1));
        }
    }
}
";
    }
}
