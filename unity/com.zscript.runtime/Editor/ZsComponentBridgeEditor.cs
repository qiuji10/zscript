// ZsComponentBridgeEditor.cs — Custom Inspector for ZsComponentBridge.
//
// Shows:
//   • VM reference and runtime instance validity
//   • @unity.serialize integration status (ZsSerializedFields present or not)
//   • Quick-add button when ZsSerializedFields is missing
//   • Annotation info read from the VM when in Play Mode
using UnityEditor;
using UnityEngine;

namespace ZScript.Editor
{
    [CustomEditor(typeof(ZsComponentBridge))]
    internal sealed class ZsComponentBridgeEditor : UnityEditor.Editor
    {
        public override void OnInspectorGUI()
        {
            var bridge = (ZsComponentBridge)target;

            // ---- VM reference ----
            EditorGUILayout.LabelField("ZScript Component Bridge", EditorStyles.boldLabel);
            EditorGUI.BeginDisabledGroup(true);
            EditorGUILayout.ObjectField("Script VM", bridge.ScriptVM,
                typeof(ZScriptVM), allowSceneObjects: true);
            EditorGUI.EndDisabledGroup();

            // ---- Instance validity ----
            bool hasInstance = bridge.Instance != null && !bridge.Instance.IsInvalid;
            EditorGUILayout.LabelField("ZScript Instance",
                hasInstance
                    ? EditorGUIUtility.IconContent("d_greenLight").text + " Valid (assigned at runtime)"
                    : "— Not yet assigned (assigned during Start)");

            EditorGUILayout.Space(8f);

            // ---- Annotations (Play Mode only) ----
            if (Application.isPlaying && bridge.ScriptVM != null && hasInstance)
            {
                DrawAnnotationInfo(bridge);
                EditorGUILayout.Space(4f);
            }

            // ---- @unity.serialize integration ----
            EditorGUILayout.LabelField("@unity.serialize Integration", EditorStyles.boldLabel);
            var sf = bridge.GetComponent<ZsSerializedFields>();
            if (sf != null)
            {
                EditorGUILayout.HelpBox(
                    "ZsSerializedFields is attached — fields will be injected before start() runs.",
                    MessageType.None);
                if (GUILayout.Button("Select ZsSerializedFields"))
                    Selection.activeObject = sf;
            }
            else
            {
                EditorGUILayout.HelpBox(
                    "No ZsSerializedFields component found.\n" +
                    "Add one to expose inspector-editable values to your @unity.serialize class.",
                    MessageType.Info);
                if (GUILayout.Button("Add ZsSerializedFields"))
                    Undo.AddComponent<ZsSerializedFields>(bridge.gameObject);
            }

            EditorGUILayout.Space(4f);

            // ---- @unity.adapter registry hint ----
            EditorGUILayout.LabelField("Adapter Registry", EditorStyles.boldLabel);
            if (Application.isPlaying && bridge.ScriptVM != null)
            {
                EditorGUILayout.HelpBox(
                    "Use vm.WrapAdapted(obj) to wrap C# objects via @unity.adapter classes.\n" +
                    "The registry is populated automatically when scripts finish loading.",
                    MessageType.None);
            }
            else
            {
                EditorGUILayout.HelpBox(
                    "Enter Play Mode to inspect the adapter registry state.",
                    MessageType.None);
            }
        }

        private static void DrawAnnotationInfo(ZsComponentBridge bridge)
        {
            // We can't easily get the class name from the ZsValueHandle at this point,
            // so we show the annotations of the VM's loaded classes for reference.
            EditorGUILayout.LabelField("Loaded Component Classes (VM)", EditorStyles.boldLabel);
            string[] classes = bridge.ScriptVM.GetComponentClasses();
            if (classes.Length == 0)
            {
                EditorGUILayout.LabelField("  None found", EditorStyles.miniLabel);
            }
            else
            {
                foreach (string cls in classes)
                    EditorGUILayout.LabelField($"  @unity.component  {cls}", EditorStyles.miniLabel);
            }
        }
    }
}
