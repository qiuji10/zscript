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
//   • public instance methods / properties / fields via generated __index/__newindex
//     wrappers with reflection fallback for unknown members
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

        // Start with runtime types that are common, stable, and useful in gameplay
        // scripts. This is intentionally allowlisted; generating all UnityEngine
        // types blindly produces noisy, fragile bindings.
        private static readonly Type[] s_unityCoreTypes =
        {
            typeof(UnityEngine.GameObject),
            typeof(UnityEngine.Transform),
            typeof(UnityEngine.Rigidbody),
            typeof(UnityEngine.Camera),
            typeof(UnityEngine.Renderer),
            typeof(UnityEngine.Material),
            typeof(UnityEngine.Collider),
        };

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

        [MenuItem("Tools/ZScript/Generate Unity Core Bindings")]
        public static void GenerateUnityCore()
        {
            Directory.CreateDirectory(OutputDir);

            int generated = 0;
            foreach (var type in s_unityCoreTypes)
            {
                string code = GenerateBinding(type, suppressStaticGlobal: true);
                if (code == null) continue;

                string path = Path.Combine(OutputDir, $"{type.Name}_ZsBinding.cs");
                File.WriteAllText(path, code, Encoding.UTF8);
                generated++;
            }

            File.WriteAllText(
                Path.Combine(OutputDir, "ZsBindingHelper.cs"),
                GenerateHelperFile(),
                Encoding.UTF8);

            AssetDatabase.Refresh();
            Debug.Log($"[ZScript] Unity Core Generator: wrote {generated} binding(s) to {OutputDir}/");
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

        private static string GenerateBinding(Type type, bool suppressStaticGlobal = false)
        {
            var attr   = type.GetCustomAttribute<ZScriptExportAttribute>();
            string zsName = !string.IsNullOrEmpty(attr?.Name) ? attr.Name : type.Name;

            var methods    = suppressStaticGlobal ? new List<MethodInfo>() : GetExportMethods(type);
            var properties = suppressStaticGlobal ? new List<PropertyInfo>() : GetExportProperties(type);
            var events     = suppressStaticGlobal ? new List<EventInfo>() : GetExportEvents(type);
            var instanceMethods = GetExportInstanceMethods(type);
            var instanceProperties = GetExportInstanceProperties(type);
            var instanceFields = GetExportInstanceFields(type);
            bool hasStaticBindings = !suppressStaticGlobal;
            bool hasInstanceBindings = !type.IsValueType &&
                (instanceMethods.Count > 0 || instanceProperties.Count > 0 || instanceFields.Count > 0);

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
            string interfaces = InterfaceList(hasStaticBindings, hasInstanceBindings);
            sb.AppendLine($"    public sealed class {type.Name}_ZsBinding{interfaces}");
            sb.AppendLine("    {");

            // Delegate-holding fields prevent GC from collecting the delegates while
            // the VM is alive, since only C++ holds a raw function pointer to them.
            sb.AppendLine("        // Held to prevent delegate GC while the VM is alive.");
            sb.AppendLine("        private readonly List<ZsNativeFn> _delegates = new List<ZsNativeFn>();");
            sb.AppendLine();

            if (hasInstanceBindings)
            {
                EmitInstanceState(sb, type, instanceMethods);
                sb.AppendLine();
            }

            if (hasStaticBindings)
            {
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
            }

            if (hasInstanceBindings)
            {
                sb.AppendLine();
                EmitInstanceBinding(sb, type, instanceMethods, instanceProperties, instanceFields);
            }

            sb.AppendLine("    }");
            sb.AppendLine("}");

            return sb.ToString();
        }

        private static string InterfaceList(bool hasStaticBindings, bool hasInstanceBindings)
        {
            var interfaces = new List<string>();
            if (hasStaticBindings) interfaces.Add("IZScriptExport");
            if (hasInstanceBindings) interfaces.Add("IZScriptInstanceExport");
            return interfaces.Count == 0 ? string.Empty : " : " + string.Join(", ", interfaces);
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

        private static List<MethodInfo> GetExportInstanceMethods(Type type)
        {
            var result = new List<MethodInfo>();
            var byName = new Dictionary<string, List<MethodInfo>>(StringComparer.Ordinal);
            foreach (var m in type.GetMethods(BindingFlags.Public | BindingFlags.Instance))
            {
                if (m.IsSpecialName || m.DeclaringType == typeof(object)) continue;
                if (m.GetCustomAttribute<ZScriptHideAttribute>() != null) continue;
                if (m.GetCustomAttribute<ObsoleteAttribute>() != null) continue;
                if (IsEditorOnlyMember(m)) continue;
                if (m.ContainsGenericParameters) continue;
                if (!CanEmitTypeReference(m.ReturnType)) continue;
                if (m.ReturnType.IsByRef || m.ReturnType.IsPointer) continue;
                if (m.GetParameters().Any(p => p.ParameterType.IsByRef || p.ParameterType.IsPointer || p.IsOut ||
                                              !CanEmitTypeReference(p.ParameterType)))
                    continue;
                if (!byName.TryGetValue(m.Name, out var group))
                {
                    group = new List<MethodInfo>();
                    byName[m.Name] = group;
                }
                group.Add(m);
            }
            foreach (var entry in byName)
            {
                if (entry.Value.Count == 1)
                    result.Add(entry.Value[0]);
            }
            return result;
        }

        private static List<PropertyInfo> GetExportInstanceProperties(Type type)
        {
            var result = new List<PropertyInfo>();
            foreach (var p in type.GetProperties(BindingFlags.Public | BindingFlags.Instance))
            {
                if (p.GetCustomAttribute<ZScriptHideAttribute>() != null) continue;
                if (p.GetCustomAttribute<ObsoleteAttribute>() != null) continue;
                if (IsEditorOnlyMember(p)) continue;
                if (p.GetIndexParameters().Length != 0) continue;
                if (!CanEmitTypeReference(p.PropertyType)) continue;
                result.Add(p);
            }
            return result;
        }

        private static List<FieldInfo> GetExportInstanceFields(Type type)
        {
            var result = new List<FieldInfo>();
            foreach (var f in type.GetFields(BindingFlags.Public | BindingFlags.Instance))
            {
                if (f.IsSpecialName) continue;
                if (f.GetCustomAttribute<ZScriptHideAttribute>() != null) continue;
                if (f.GetCustomAttribute<ObsoleteAttribute>() != null) continue;
                if (IsEditorOnlyMember(f)) continue;
                if (!CanEmitTypeReference(f.FieldType)) continue;
                result.Add(f);
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

        private static void EmitInstanceState(StringBuilder sb, Type type, List<MethodInfo> methods)
        {
            sb.AppendLine("        private ZsNativeFn _instanceIndex;");
            sb.AppendLine("        private ZsNativeFn _instanceNewIndex;");
            foreach (var m in methods)
                sb.AppendLine($"        private ZsNativeFn _instanceMethod_{m.Name};");
        }

        private static void EmitInstanceBinding(StringBuilder sb, Type type,
            List<MethodInfo> methods, List<PropertyInfo> properties, List<FieldInfo> fields)
        {
            sb.AppendLine($"        public Type TargetType => typeof({type.FullName});");
            sb.AppendLine();
            sb.AppendLine("        public IntPtr WrapRaw(ZScriptVM vm, object obj)");
            sb.AppendLine("        {");
            sb.AppendLine($"            if (!(obj is {type.FullName})) return ZsNative.zs_value_nil();");
            sb.AppendLine("            EnsureInstanceBindings(vm);");
            sb.AppendLine("            IntPtr proxy = ZsNative.zs_vm_push_object_handle(vm.RawVM, vm.ObjectPool.Alloc(obj));");
            sb.AppendLine("            ZsNative.zs_vm_handle_set_index(vm.RawVM, proxy, _instanceIndex);");
            sb.AppendLine("            ZsNative.zs_vm_handle_set_newindex(vm.RawVM, proxy, _instanceNewIndex);");
            sb.AppendLine("            return proxy;");
            sb.AppendLine("        }");
            sb.AppendLine();
            sb.AppendLine("        private void EnsureInstanceBindings(ZScriptVM vm)");
            sb.AppendLine("        {");
            sb.AppendLine("            if (_instanceIndex != null) return;");
            sb.AppendLine("            var reflection = vm.ReflectionProxy;");
            sb.AppendLine();

            foreach (var m in methods)
                EmitInstanceMethod(sb, type, m);

            EmitInstanceIndex(sb, type, methods, properties, fields);
            sb.AppendLine();
            EmitInstanceNewIndex(sb, type, properties, fields);
            sb.AppendLine("        }");
        }

        private static void EmitInstanceMethod(StringBuilder sb, Type type, MethodInfo m)
        {
            string fnField = $"_instanceMethod_{m.Name}";
            var @params = m.GetParameters();

            sb.AppendLine($"            {fnField} = (_, argc, argv) =>");
            sb.AppendLine("            {");
            sb.AppendLine("                if (argc < 1) return ZsNative.zs_value_nil();");
            sb.AppendLine("                long selfId = ZsNative.zs_vm_get_object_handle(vm.RawVM, argv[0]);");
            sb.AppendLine($"                var self = vm.ObjectPool.Get(selfId) as {type.FullName};");
            sb.AppendLine("                if (self == null) return ZsNative.zs_value_nil();");
            sb.AppendLine("                try");
            sb.AppendLine("                {");
            for (int i = 0; i < @params.Length; i++)
            {
                var p = @params[i];
                string local = $"arg_{p.Name}";
                sb.AppendLine($"                    {CsTypeName(p.ParameterType)} {local} = argc > {i + 1}");
                sb.AppendLine($"                        ? ({CsTypeName(p.ParameterType)})reflection.FromZsValue(argv[{i + 1}], typeof({CsTypeName(p.ParameterType)}))");
                sb.AppendLine("                        : default;");
            }

            string argList = string.Join(", ", @params.Select(p => $"arg_{p.Name}"));
            if (m.ReturnType == typeof(void))
            {
                sb.AppendLine($"                    ZScriptVM.TraceBinding(\"generated call {type.Name}.{m.Name}\");");
                sb.AppendLine($"                    self.{m.Name}({argList});");
                sb.AppendLine("                    return ZsNative.zs_value_nil();");
            }
            else
            {
                sb.AppendLine($"                    ZScriptVM.TraceBinding(\"generated call {type.Name}.{m.Name}\");");
                sb.AppendLine($"                    var ret = self.{m.Name}({argList});");
                sb.AppendLine($"                    return reflection.ToZsValue(ret, typeof({CsTypeName(m.ReturnType)}));");
            }
            sb.AppendLine("                }");
            sb.AppendLine("                catch (Exception e)");
            sb.AppendLine("                {");
            sb.AppendLine($"                    UnityEngine.Debug.LogError($\"[ZScript] {type.Name}.{m.Name} failed: {{e.InnerException?.Message ?? e.Message}}\");");
            sb.AppendLine("                    return ZsNative.zs_value_nil();");
            sb.AppendLine("                }");
            sb.AppendLine("            };");
            sb.AppendLine($"            _delegates.Add({fnField});");
            sb.AppendLine();
        }

        private static void EmitInstanceIndex(StringBuilder sb, Type type,
            List<MethodInfo> methods, List<PropertyInfo> properties, List<FieldInfo> fields)
        {
            sb.AppendLine("            _instanceIndex = (_, argc, argv) =>");
            sb.AppendLine("            {");
            sb.AppendLine("                if (argc < 2) return ZsNative.zs_value_nil();");
            sb.AppendLine("                long selfId = ZsNative.zs_vm_get_object_handle(vm.RawVM, argv[0]);");
            sb.AppendLine($"                var self = vm.ObjectPool.Get(selfId) as {type.FullName};");
            sb.AppendLine("                if (self == null) return ZsNative.zs_value_nil();");
            sb.AppendLine("                string key = ZsBindingHelper.ReadString(argv[1]);");
            sb.AppendLine("                try");
            sb.AppendLine("                {");
            sb.AppendLine("                    switch (key)");
            sb.AppendLine("                    {");
            foreach (var p in properties.Where(HasPublicGetter))
            {
                sb.AppendLine($"                        case \"{p.Name}\":");
                sb.AppendLine($"                            ZScriptVM.TraceBinding(\"generated get {type.Name}.{p.Name}\");");
                sb.AppendLine($"                            return reflection.ToZsValue(self.{p.Name}, typeof({CsTypeName(p.PropertyType)}));");
            }
            foreach (var f in fields)
            {
                sb.AppendLine($"                        case \"{f.Name}\":");
                sb.AppendLine($"                            ZScriptVM.TraceBinding(\"generated get {type.Name}.{f.Name}\");");
                sb.AppendLine($"                            return reflection.ToZsValue(self.{f.Name}, typeof({CsTypeName(f.FieldType)}));");
            }
            foreach (var m in methods)
            {
                sb.AppendLine($"                        case \"{m.Name}\":");
                sb.AppendLine($"                            ZScriptVM.TraceBinding(\"generated get {type.Name}.{m.Name} (method)\");");
                sb.AppendLine($"                            return ZsNative.zs_vm_make_native_fn(vm.RawVM, \"{m.Name}\", _instanceMethod_{m.Name});");
            }
            sb.AppendLine("                    }");
            sb.AppendLine("                }");
            sb.AppendLine("                catch (Exception e)");
            sb.AppendLine("                {");
            sb.AppendLine($"                    UnityEngine.Debug.LogError($\"[ZScript] {type.Name}.__index failed: {{e.InnerException?.Message ?? e.Message}}\");");
            sb.AppendLine("                    return ZsNative.zs_value_nil();");
            sb.AppendLine("                }");
            sb.AppendLine($"                ZScriptVM.TraceBinding(\"generated fallback {type.Name}.\" + key + \" -> reflection\");");
            sb.AppendLine("                return reflection.GetMember(self, key);");
            sb.AppendLine("            };");
            sb.AppendLine("            _delegates.Add(_instanceIndex);");
        }

        private static void EmitInstanceNewIndex(StringBuilder sb, Type type,
            List<PropertyInfo> properties, List<FieldInfo> fields)
        {
            sb.AppendLine("            _instanceNewIndex = (_, argc, argv) =>");
            sb.AppendLine("            {");
            sb.AppendLine("                if (argc < 3) return ZsNative.zs_value_nil();");
            sb.AppendLine("                long selfId = ZsNative.zs_vm_get_object_handle(vm.RawVM, argv[0]);");
            sb.AppendLine($"                var self = vm.ObjectPool.Get(selfId) as {type.FullName};");
            sb.AppendLine("                if (self == null) return ZsNative.zs_value_nil();");
            sb.AppendLine("                string key = ZsBindingHelper.ReadString(argv[1]);");
            sb.AppendLine("                try");
            sb.AppendLine("                {");
            sb.AppendLine("                    switch (key)");
            sb.AppendLine("                    {");
            foreach (var p in properties.Where(HasPublicSetter))
            {
                sb.AppendLine($"                        case \"{p.Name}\":");
                sb.AppendLine($"                            ZScriptVM.TraceBinding(\"generated set {type.Name}.{p.Name}\");");
                sb.AppendLine($"                            self.{p.Name} = ({CsTypeName(p.PropertyType)})reflection.FromZsValue(argv[2], typeof({CsTypeName(p.PropertyType)}));");
                sb.AppendLine("                            return ZsNative.zs_value_nil();");
            }
            foreach (var f in fields.Where(f => !f.IsInitOnly))
            {
                sb.AppendLine($"                        case \"{f.Name}\":");
                sb.AppendLine($"                            ZScriptVM.TraceBinding(\"generated set {type.Name}.{f.Name}\");");
                sb.AppendLine($"                            self.{f.Name} = ({CsTypeName(f.FieldType)})reflection.FromZsValue(argv[2], typeof({CsTypeName(f.FieldType)}));");
                sb.AppendLine("                            return ZsNative.zs_value_nil();");
            }
            sb.AppendLine("                    }");
            sb.AppendLine("                }");
            sb.AppendLine("                catch (Exception e)");
            sb.AppendLine("                {");
            sb.AppendLine($"                    UnityEngine.Debug.LogError($\"[ZScript] {type.Name}.__newindex failed: {{e.InnerException?.Message ?? e.Message}}\");");
            sb.AppendLine("                    return ZsNative.zs_value_nil();");
            sb.AppendLine("                }");
            sb.AppendLine($"                ZScriptVM.TraceBinding(\"generated fallback set {type.Name}.\" + key + \" -> reflection\");");
            sb.AppendLine("                reflection.SetMember(self, key, argv[2]);");
            sb.AppendLine("                return ZsNative.zs_value_nil();");
            sb.AppendLine("            };");
            sb.AppendLine("            _delegates.Add(_instanceNewIndex);");
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

        private static bool HasPublicGetter(PropertyInfo p)
            => p.GetMethod != null && p.GetMethod.IsPublic;

        private static bool HasPublicSetter(PropertyInfo p)
            => p.SetMethod != null && p.SetMethod.IsPublic;

        private static bool CanEmitTypeReference(Type t)
        {
            if (t == typeof(void)) return true;
            if (t.IsByRef || t.IsPointer || t.IsGenericType || t.IsArray) return false;
            if (t.GetCustomAttribute<ObsoleteAttribute>() != null) return false;
            if (t.FullName != null && t.FullName.Contains("NetworkView")) return false;
            return !string.IsNullOrEmpty(t.FullName) || t.IsPrimitive;
        }

        private static bool IsEditorOnlyMember(MemberInfo m)
            => m.Name.IndexOf("SceneView", StringComparison.OrdinalIgnoreCase) >= 0;

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
            return t.FullName.Replace("+", ".");
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
