// Program.cs — ZScriptExportGen entry point.
//
// Usage:
//   ZScriptExportGen <assembly-path>... --cpp <cpp-output-dir> --cs <cs-output-path>
//
// Scans the specified assemblies for [ZScriptExport] types and generates:
//   <cpp-output-dir>/<TypeName>_export.cpp    — C++ binding snippet per type
//   <cpp-output-dir>/register_all_exports.cpp — bulk entry point
//   <cs-output-path>                           — ZsExportDispatch.cs stubs
//
// Invoked by:
//   - Unity Editor post-build step (via Editor script calling Process.Start)
//   - CI pipeline after user assembly changes
using ZScriptExportGen;

var assemblies  = new List<string>();
string? cppDir  = null;
string? csPath  = null;

for (int i = 0; i < args.Length; i++)
{
    if (args[i] == "--cpp" && i + 1 < args.Length) { cppDir = args[++i]; }
    else if (args[i] == "--cs" && i + 1 < args.Length) { csPath = args[++i]; }
    else                                                 { assemblies.Add(args[i]); }
}

if (assemblies.Count == 0 || cppDir == null || csPath == null)
{
    Console.Error.WriteLine(
        "Usage: ZScriptExportGen <assembly.dll>... --cpp <cpp-dir> --cs <cs-path>");
    return 1;
}

Console.WriteLine($"[info] Scanning {assemblies.Count} assemblies for [ZScriptExport]...");
var types = ExportScanner.Scan(assemblies);

if (types.Count == 0)
{
    Console.WriteLine("[info] No [ZScriptExport] types found. Nothing generated.");
    return 0;
}

Console.WriteLine($"[info] Found {types.Count} exported type(s):");
foreach (var t in types)
    Console.WriteLine($"       {t.CsName} → \"{t.ZsName}\" ({t.Kind}, {t.Members.Length} member(s))");

ExportCodeWriter.WriteAll(types, cppDir, csPath);
Console.WriteLine($"[info] C++ snippets → {cppDir}");
Console.WriteLine($"[info] C# stubs     → {csPath}");
Console.WriteLine("[info] Done.");
return 0;
