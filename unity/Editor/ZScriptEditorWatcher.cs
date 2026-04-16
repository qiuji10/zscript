// ZScriptEditorWatcher.cs — Watches the ZScripts/ folder for changes and
// reloads modified scripts into any active ZScriptVM in Play mode.
//
// Activated automatically by [InitializeOnLoad]; no manual setup required.
// The watcher runs only in the Editor (this file lives under Editor/).
//
// Watched directory: <project>/ZScripts/ (configurable via EditorPrefs key
// "ZScript.WatchDir"; relative paths resolved from Application.dataPath).
//
using System.IO;
using UnityEditor;
using UnityEngine;

namespace ZScript.Editor
{
    [InitializeOnLoad]
    internal static class ZScriptEditorWatcher
    {
        private const string WatchDirKey = "ZScript.WatchDir";
        private static FileSystemWatcher _watcher;

        static ZScriptEditorWatcher()
        {
            EditorApplication.playModeStateChanged += OnPlayModeChanged;
            // Start watching immediately if already in Play mode (domain reload).
            if (EditorApplication.isPlaying) StartWatcher();
        }

        private static void OnPlayModeChanged(PlayModeStateChange state)
        {
            switch (state)
            {
                case PlayModeStateChange.EnteredPlayMode:
                    // Ensure scripts are up-to-date when Play mode starts.
                    ReloadAllScripts();
                    StartWatcher();
                    break;

                case PlayModeStateChange.ExitingPlayMode:
                    StopWatcher();
                    break;
            }
        }

        // ----------------------------------------------------------------
        // FileSystemWatcher management
        // ----------------------------------------------------------------
        private static void StartWatcher()
        {
            StopWatcher();

            string dir = ResolveWatchDir();
            if (!Directory.Exists(dir))
            {
                // Directory may not exist yet; silently skip.
                return;
            }

            _watcher = new FileSystemWatcher(dir, "*.zs")
            {
                IncludeSubdirectories = true,
                NotifyFilter          = NotifyFilters.LastWrite | NotifyFilters.FileName,
                EnableRaisingEvents   = true,
            };
            _watcher.Changed += OnFileChanged;
            _watcher.Created += OnFileChanged;
            _watcher.Renamed += (s, e) => OnFileChanged(s, new FileSystemEventArgs(
                WatcherChangeTypes.Changed, Path.GetDirectoryName(e.FullPath), e.Name));
        }

        private static void StopWatcher()
        {
            if (_watcher == null) return;
            _watcher.EnableRaisingEvents = false;
            _watcher.Dispose();
            _watcher = null;
        }

        private static void OnFileChanged(object sender, FileSystemEventArgs e)
        {
            // FileSystemWatcher fires on a background thread; schedule reload
            // on the main thread via EditorApplication.delayCall.
            string path = e.FullPath;
            EditorApplication.delayCall += () => ReloadScript(path);
        }

        // ----------------------------------------------------------------
        // Script reload helpers
        // ----------------------------------------------------------------
        private static void ReloadScript(string fullPath)
        {
            if (!EditorApplication.isPlaying) return;
            if (!File.Exists(fullPath)) return;

            var vms = Object.FindObjectsOfType<ZScriptVM>();
            if (vms.Length == 0) return;

            string source;
            try { source = File.ReadAllText(fullPath); }
            catch { return; }

            string name = Path.GetFileNameWithoutExtension(fullPath);
            foreach (var vm in vms)
            {
                if (vm.enableHotpatch)
                    vm.LoadSource(name, source);
            }
            Debug.Log($"[ZScript] Hot-reloaded '{name}' ({vms.Length} VM(s)).");
        }

        private static void ReloadAllScripts()
        {
            string dir = ResolveWatchDir();
            if (!Directory.Exists(dir)) return;

            var vms = Object.FindObjectsOfType<ZScriptVM>();
            if (vms.Length == 0) return;

            foreach (string path in Directory.GetFiles(dir, "*.zs", SearchOption.AllDirectories))
            {
                string source;
                try { source = File.ReadAllText(path); }
                catch { continue; }

                string name = Path.GetFileNameWithoutExtension(path);
                foreach (var vm in vms)
                    vm.LoadSource(name, source);
            }
        }

        // ----------------------------------------------------------------
        // Watch directory resolution
        // ----------------------------------------------------------------
        internal static string ResolveWatchDir()
        {
            string pref = EditorPrefs.GetString(WatchDirKey, "ZScripts");
            return Path.IsPathRooted(pref)
                ? pref
                : Path.Combine(Application.dataPath, pref);
        }

        internal static void SetWatchDir(string dir)
            => EditorPrefs.SetString(WatchDirKey, dir);
    }
}
