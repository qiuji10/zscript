// ZScriptModule.cs — ScriptableObject asset referencing a single .zs file.
// Create via Assets > Create > ZScript > Script Module in the Unity Editor.
// Drag onto ZScriptVM.startupModules in the Inspector; the VM loads each
// module in list order during Start().
using UnityEngine;

namespace ZScript
{
    /// <summary>
    /// A ScriptableObject that wraps a .zs script asset.  Drag-and-drop into
    /// the ZScriptVM.startupModules list to have the script loaded on Start().
    /// </summary>
    [CreateAssetMenu(
        fileName  = "NewZScriptModule",
        menuName  = "ZScript/Script Module",
        order     = 100)]
    public class ZScriptModule : ScriptableObject
    {
        [Tooltip("The .zs script file to load (must be inside StreamingAssets " +
                 "or supplied as inline source).")]
        public TextAsset scriptAsset;

        [Tooltip("Logical name used as the chunk name for error messages. " +
                 "Defaults to the asset name if left blank.")]
        public string moduleName;

        /// <summary>
        /// Load this module into the given VM.  Uses inline source from
        /// scriptAsset.text so the file does not need to be in StreamingAssets
        /// at runtime (it is embedded by Unity's asset pipeline).
        /// Returns true on success.
        /// </summary>
        public bool LoadInto(ZScriptVM vm)
        {
            if (vm == null)
            {
                Debug.LogError("[ZScript] ZScriptModule.LoadInto: vm is null.");
                return false;
            }
            if (scriptAsset == null)
            {
                Debug.LogError("[ZScript] ZScriptModule '" + name +
                               "': no script asset assigned.");
                return false;
            }

            string chunkName = string.IsNullOrEmpty(moduleName) ? name : moduleName;
            return vm.LoadSource(chunkName, scriptAsset.text);
        }
    }
}
