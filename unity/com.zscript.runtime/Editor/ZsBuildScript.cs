// ZsBuildScript.cs — Headless CI helper for IL2CPP / link.xml validation.
//
// Called from CI via:
//   Unity.exe -batchmode -nographics -projectPath ... -executeMethod ZScript.Editor.ZsBuildScript.ValidateLinkXml -logFile ...
//
// What it does:
//   1. Regenerates Assets/link.xml from the current project's assemblies
//      (same logic as Tools > ZScript > Generate IL2CPP link.xml, batch-mode safe).
//   2. Parses the generated file and checks every <type> entry resolves to a real
//      type in the currently loaded AppDomain — i.e. IL2CPP's stripper would find it.
//   3. Validates the minimum required entries (ZScript.Runtime, ZScriptGenerated).
//   4. Exits with code 0 on pass, 1 on failure so the CI step fails the job.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Xml.Linq;
using UnityEditor;
using UnityEngine;

namespace ZScript.Editor
{
    public static class ZsBuildScript
    {
        private const string LinkXmlPath = "Assets/link.xml";

        [MenuItem("Tools/ZScript/Validate IL2CPP link.xml (CI)")]
        public static void ValidateLinkXml()
        {
            var errors = new List<string>();

            try
            {
                // Step 1: regenerate link.xml (batch-mode safe — no dialog shown).
                ZsLinkXmlGenerator.Generate();

                if (!File.Exists(LinkXmlPath))
                {
                    errors.Add($"link.xml was not written to {LinkXmlPath}");
                    Finish(errors);
                    return;
                }

                // Step 2: parse and validate.
                XDocument doc;
                try
                {
                    doc = XDocument.Load(LinkXmlPath);
                }
                catch (Exception ex)
                {
                    errors.Add($"link.xml parse error: {ex.Message}");
                    Finish(errors);
                    return;
                }

                XElement root = doc.Root;
                if (root == null || root.Name.LocalName != "linker")
                {
                    errors.Add("link.xml root element must be <linker>.");
                    Finish(errors);
                    return;
                }

                // Rule A: ZScript.Runtime must be fully preserved.
                bool runtimeOk = root.Elements("assembly")
                    .Any(a => (string)a.Attribute("fullname") == "ZScript.Runtime" &&
                              (string)a.Attribute("preserve") == "all");
                if (!runtimeOk)
                    errors.Add("Missing: <assembly fullname=\"ZScript.Runtime\" preserve=\"all\"/>");

                // Rule B: ZScriptGenerated namespace entry must be present.
                bool generatedOk = root.Elements("assembly")
                    .SelectMany(a => a.Elements("namespace"))
                    .Any(ns => (string)ns.Attribute("fullname") == "ZScriptGenerated" &&
                               (string)ns.Attribute("preserve") == "all");
                if (!generatedOk)
                    errors.Add("Missing: <namespace fullname=\"ZScriptGenerated\" preserve=\"all\"/> " +
                               "inside an Assembly-CSharp entry.");

                // Rule C: every <type> entry must resolve to a real type.
                var allTypeNames = new HashSet<string>(
                    AppDomain.CurrentDomain.GetAssemblies()
                        .SelectMany(a => SafeGetTypes(a))
                        .Select(t => t.FullName),
                    StringComparer.Ordinal);

                foreach (var typeEl in root.Descendants("type"))
                {
                    string fullname = (string)typeEl.Attribute("fullname");
                    if (string.IsNullOrEmpty(fullname))
                    {
                        errors.Add("Found <type> element with empty or missing fullname attribute.");
                        continue;
                    }
                    if (!allTypeNames.Contains(fullname))
                        errors.Add($"link.xml preserves type not found in any loaded assembly: {fullname}");
                }
            }
            catch (Exception ex)
            {
                errors.Add($"Unhandled exception during validation: {ex}");
            }

            Finish(errors);
        }

        // ----------------------------------------------------------------

        private static void Finish(List<string> errors)
        {
            if (errors.Count == 0)
            {
                Debug.Log("[ZScript] link.xml validation passed — all preserved types resolve correctly.");
                if (Application.isBatchMode)
                    EditorApplication.Exit(0);
            }
            else
            {
                foreach (string e in errors)
                    Debug.LogError($"[ZScript] link.xml validation FAIL: {e}");
                Debug.LogError($"[ZScript] {errors.Count} error(s). IL2CPP build may strip required types.");
                if (Application.isBatchMode)
                    EditorApplication.Exit(1);
            }
        }

        private static IEnumerable<Type> SafeGetTypes(Assembly asm)
        {
            try   { return asm.GetTypes(); }
            catch { return Array.Empty<Type>(); }
        }
    }
}
