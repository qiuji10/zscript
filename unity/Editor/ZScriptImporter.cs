// ZScriptImporter.cs — ScriptedImporter for .zs files.
//
// Registers .zs as a Unity asset type so that:
//   • Unity tracks .zs files in the AssetDatabase (import pipeline).
//   • Double-clicking a .zs asset opens it in the external editor (VS Code / Rider).
//   • Syntax errors from a `zsc check` pass appear in the Console with file+line links.
//   • .zs files can be assigned to ZScriptModule assets or dragged into the
//     ZScriptVM.startupScripts list.
//
// The importer produces a ZScriptTextAsset — a thin TextAsset subclass that
// carries the raw source text so it can be loaded at runtime via Resources.Load.
//
using System;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEditor.AssetImporters;
using UnityEngine;

namespace ZScript.Editor
{
    [ScriptedImporter(version: 1, ext: "zs")]
    internal sealed class ZScriptImporter : ScriptedImporter
    {
        // ------------------------------------------------------------------
        // Inspector options
        // ------------------------------------------------------------------
        [Tooltip("Run a compile-check on import and log errors to the Console.")]
        public bool checkOnImport = true;

        // ------------------------------------------------------------------
        // Import
        // ------------------------------------------------------------------
        public override void OnImportAsset(AssetImportContext ctx)
        {
            string source;
            try { source = File.ReadAllText(ctx.assetPath, Encoding.UTF8); }
            catch (Exception ex)
            {
                ctx.LogImportError($"[ZScript] Cannot read '{ctx.assetPath}': {ex.Message}");
                return;
            }

            // Create the primary asset: a TextAsset containing the raw source.
            var textAsset = new TextAsset(source)
            {
                name = Path.GetFileNameWithoutExtension(ctx.assetPath)
            };
            ctx.AddObjectToAsset("source", textAsset);
            ctx.SetMainObject(textAsset);

            // Optionally validate the syntax.
            if (checkOnImport)
                CompileCheck(ctx, source);
        }

        // ------------------------------------------------------------------
        // Syntax check via in-process VM
        // ------------------------------------------------------------------
        private static void CompileCheck(AssetImportContext ctx, string source)
        {
            // Spin up a temporary VM just for compile validation.
            IntPtr vm = ZsNative.zs_vm_new();
            try
            {
                byte[] err = new byte[1024];
                string name = Path.GetFileNameWithoutExtension(ctx.assetPath);
                int ok = ZsNative.zs_vm_load_source(vm, name, source, err, err.Length);
                if (ok == 0)
                {
                    string msg = Encoding.UTF8.GetString(err).TrimEnd('\0');
                    ctx.LogImportError($"[ZScript] {msg}  ({ctx.assetPath})");
                }
            }
            finally
            {
                ZsNative.zs_vm_free(vm);
            }
        }
    }

    // ======================================================================
    // Custom Inspector for ZScript text assets
    // ======================================================================
    [CustomEditor(typeof(TextAsset))]
    internal sealed class ZScriptAssetEditor : UnityEditor.Editor
    {
        private static readonly string[] _zsExt = { ".zs" };
        private Vector2 _scroll;

        public override void OnInspectorGUI()
        {
            string path = AssetDatabase.GetAssetPath(target);
            if (!path.EndsWith(".zs", StringComparison.OrdinalIgnoreCase))
            {
                // Not a .zs asset — fall back to default inspector.
                DrawDefaultInspector();
                return;
            }

            var asset = (TextAsset)target;

            // Toolbar
            EditorGUILayout.BeginHorizontal(EditorStyles.toolbar);
            GUILayout.Label(Path.GetFileName(path), EditorStyles.boldLabel,
                GUILayout.ExpandWidth(true));
            if (GUILayout.Button("Open", EditorStyles.toolbarButton, GUILayout.Width(48)))
                AssetDatabase.OpenAsset(target);
            EditorGUILayout.EndHorizontal();

            // Read-only source preview (first 200 lines).
            _scroll = EditorGUILayout.BeginScrollView(_scroll,
                GUILayout.Height(Mathf.Min(Screen.height * 0.6f, 400)));
            EditorGUILayout.TextArea(asset.text, EditorStyles.wordWrappedMiniLabel,
                GUILayout.ExpandHeight(true));
            EditorGUILayout.EndScrollView();
        }
    }
}
