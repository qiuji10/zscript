// ZScriptEditorWindow.cs — Unity Editor window for ZScript diagnostics.
//
// Open via:  Window > ZScript
//
// Shows:
//   • All ZScriptVM instances in the current scene
//   • Per-VM: loaded module names, hotpatch status, object pool count, last error
//   • Manual "Reload All" button (Play mode only)
//   • Watch directory setting
//
using System.IO;
using UnityEditor;
using UnityEngine;

namespace ZScript.Editor
{
    internal sealed class ZScriptEditorWindow : EditorWindow
    {
        [MenuItem("Window/ZScript")]
        private static void Open() => GetWindow<ZScriptEditorWindow>("ZScript").Show();

        // ----------------------------------------------------------------
        // State
        // ----------------------------------------------------------------
        private Vector2 _scroll;
        private double  _lastRefresh;
        private const double RefreshInterval = 0.5; // seconds

        // ----------------------------------------------------------------
        // GUI
        // ----------------------------------------------------------------
        private void OnGUI()
        {
            // Auto-refresh while in Play mode.
            if (EditorApplication.isPlaying && EditorApplication.timeSinceStartup - _lastRefresh > RefreshInterval)
            {
                _lastRefresh = EditorApplication.timeSinceStartup;
                Repaint();
            }

            DrawHeader();
            EditorGUILayout.Space(4);
            DrawSettings();
            EditorGUILayout.Space(8);
            DrawVMList();
        }

        private void DrawHeader()
        {
            EditorGUILayout.BeginHorizontal(EditorStyles.toolbar);
            GUILayout.Label("ZScript", EditorStyles.boldLabel, GUILayout.ExpandWidth(true));

            using (new EditorGUI.DisabledScope(!EditorApplication.isPlaying))
            {
                if (GUILayout.Button("Reload All", EditorStyles.toolbarButton, GUILayout.Width(80)))
                    ReloadAll();
            }
            EditorGUILayout.EndHorizontal();
        }

        private void DrawSettings()
        {
            EditorGUILayout.LabelField("Watch Directory", EditorStyles.boldLabel);
            EditorGUI.indentLevel++;

            string current = ZScriptEditorWatcher.ResolveWatchDir();
            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.LabelField(current, EditorStyles.miniLabel, GUILayout.ExpandWidth(true));
            if (GUILayout.Button("Browse…", GUILayout.Width(60)))
            {
                string chosen = EditorUtility.OpenFolderPanel(
                    "ZScript Watch Directory", Application.dataPath, "ZScripts");
                if (!string.IsNullOrEmpty(chosen))
                    ZScriptEditorWatcher.SetWatchDir(chosen);
            }
            EditorGUILayout.EndHorizontal();
            EditorGUI.indentLevel--;
        }

        private void DrawVMList()
        {
            var vms = FindObjectsByType<ZScriptVM>(FindObjectsSortMode.None);

            if (vms.Length == 0)
            {
                EditorGUILayout.HelpBox(
                    "No ZScriptVM found in the current scene.",
                    EditorApplication.isPlaying ? MessageType.Warning : MessageType.Info);
                return;
            }

            _scroll = EditorGUILayout.BeginScrollView(_scroll);

            foreach (var vm in vms)
                DrawVM(vm);

            EditorGUILayout.EndScrollView();
        }

        private void DrawVM(ZScriptVM vm)
        {
            if (vm == null) return;

            EditorGUILayout.BeginVertical(GUI.skin.box);

            // Header row: name + object selector.
            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.ObjectField(vm.gameObject, typeof(GameObject), true,
                GUILayout.ExpandWidth(true));
            GUILayout.Label(vm.enableHotpatch ? "hotpatch ON" : "hotpatch OFF",
                EditorStyles.miniLabel, GUILayout.Width(85));
            EditorGUILayout.EndHorizontal();

            // Object pool count.
            if (EditorApplication.isPlaying)
            {
                int count = vm.ObjectPool.Count;
                EditorGUILayout.LabelField("Object pool handles", count.ToString(),
                    EditorStyles.miniLabel);
            }

            // Tags.
            if (vm.tags != null && vm.tags.Count > 0)
                EditorGUILayout.LabelField("Tags", string.Join(", ", vm.tags),
                    EditorStyles.miniLabel);

            // Startup modules.
            if (vm.startupModules != null && vm.startupModules.Count > 0)
            {
                EditorGUI.indentLevel++;
                foreach (var mod in vm.startupModules)
                {
                    if (mod != null)
                        EditorGUILayout.LabelField("module", mod.name, EditorStyles.miniLabel);
                }
                EditorGUI.indentLevel--;
            }

            // Startup scripts.
            if (vm.startupScripts != null && vm.startupScripts.Count > 0)
            {
                EditorGUI.indentLevel++;
                foreach (var s in vm.startupScripts)
                    EditorGUILayout.LabelField("script", s, EditorStyles.miniLabel);
                EditorGUI.indentLevel--;
            }

            EditorGUILayout.EndVertical();
            EditorGUILayout.Space(2);
        }

        // ----------------------------------------------------------------
        // Reload
        // ----------------------------------------------------------------
        private static void ReloadAll()
        {
            if (!EditorApplication.isPlaying) return;

            string dir = ZScriptEditorWatcher.ResolveWatchDir();
            if (!Directory.Exists(dir))
            {
                Debug.LogWarning("[ZScript] Watch directory not found: " + dir);
                return;
            }

            var vms = FindObjectsByType<ZScriptVM>(FindObjectsSortMode.None);
            int reloaded = 0;
            foreach (string path in Directory.GetFiles(dir, "*.zs", SearchOption.AllDirectories))
            {
                string source;
                try { source = File.ReadAllText(path); }
                catch { continue; }
                string name = Path.GetFileNameWithoutExtension(path);
                foreach (var vm in vms)
                    vm.LoadSource(name, source);
                reloaded++;
            }
            Debug.Log($"[ZScript] Manual reload: {reloaded} script(s) into {vms.Length} VM(s).");
        }

        private void OnEnable()  => EditorApplication.update += Repaint;
        private void OnDisable() => EditorApplication.update -= Repaint;
    }
}
