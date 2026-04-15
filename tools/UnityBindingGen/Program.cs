// Program.cs — UnityBindingGen entry point.
//
// Usage:
//   UnityBindingGen <unity-managed-dir> <cpp-output-dir> <cs-output-path> [--all]
//
// Arguments:
//   unity-managed-dir  Path to Unity's Managed/ folder containing UnityEngine DLLs.
//                      e.g. /Applications/Unity/Hub/Editor/2023.2.0f1/Unity.app/
//                           Contents/Managed/UnityEngine/
//   cpp-output-dir     Directory where per-type *_binding.cpp files are written.
//                      e.g. src/unity/bindings/
//   cs-output-path     Path for the generated ZsUnityDispatch.cs file.
//                      e.g. unity/Runtime/ZsUnityDispatch.cs
//   --all              Scan all public types (default: priority list only).
//
// The tool is invoked from:
//   - Unity Editor menu: Tools > ZScript > Regenerate Bindings
//   - CI pipeline after a Unity version upgrade
using UnityBindingGen;

if (args.Length < 3)
{
    Console.Error.WriteLine(
        "Usage: UnityBindingGen <unity-managed-dir> <cpp-output-dir> <cs-output-path> [--all]");
    return 1;
}

string managedDir    = args[0];
string cppOutputDir  = args[1];
string csOutputPath  = args[2];
bool   scanAll       = args.Contains("--all", StringComparer.OrdinalIgnoreCase);

// ── Locate DLLs ─────────────────────────────────────────────────────────────
if (!Directory.Exists(managedDir))
{
    Console.Error.WriteLine($"[error] Unity managed dir not found: {managedDir}");
    return 1;
}

var dllPaths = Directory.GetFiles(managedDir, "UnityEngine*.dll", SearchOption.TopDirectoryOnly)
    .Concat(Directory.GetFiles(managedDir, "UnityEngine*.dll", SearchOption.AllDirectories))
    .Distinct()
    .ToArray();

if (dllPaths.Length == 0)
{
    Console.Error.WriteLine($"[error] No UnityEngine*.dll found in {managedDir}");
    return 1;
}

Console.WriteLine($"[info] Scanning {dllPaths.Length} DLL(s) in {managedDir}");

// ── Scan ────────────────────────────────────────────────────────────────────
var filter = scanAll ? null : (IEnumerable<string>)AssemblyScanner.PriorityTypes;
var types  = AssemblyScanner.Scan(dllPaths, filter);

if (types.Count == 0)
{
    Console.Error.WriteLine("[warn] No matching types found — check managed dir and filter.");
    return 0;
}

Console.WriteLine($"[info] Found {types.Count} type(s). Generating bindings...");

// ── Generate C++ bindings ────────────────────────────────────────────────────
CppBindingWriter.WriteAll(types, cppOutputDir);
Console.WriteLine($"[info] C++ bindings written to {cppOutputDir}");

// ── Generate C# dispatch ─────────────────────────────────────────────────────
Directory.CreateDirectory(Path.GetDirectoryName(csOutputPath)!);
CSharpDispatchWriter.Write(types, csOutputPath);
Console.WriteLine($"[info] C# dispatch written to {csOutputPath}");

// ── Build-time warning: list covered types ───────────────────────────────────
var priorityCovered = AssemblyScanner.PriorityTypes
    .Where(name => types.Any(t => t.CsName == name))
    .ToArray();
var priorityMissing = AssemblyScanner.PriorityTypes
    .Except(priorityCovered)
    .ToArray();

if (priorityMissing.Length > 0)
    Console.WriteLine($"[warn] Priority types not found in scanned DLLs: {string.Join(", ", priorityMissing)}");

Console.WriteLine("[info] Done.");
return 0;
