// ZsSerializedFieldsEditor.cs — Custom Inspector for ZsSerializedFields.
//
// Shows a compact field table where each row has:
//   [Name] [Type dropdown] [Value (type-appropriate)] [✕ remove]
// And an "Add Field" button at the bottom.
//
// Uses SerializedProperty throughout so undo, redo, prefab overrides, and
// multi-object editing all work correctly.
using UnityEditor;
using UnityEngine;

namespace ZScript.Editor
{
    [CustomEditor(typeof(ZsSerializedFields))]
    internal sealed class ZsSerializedFieldsEditor : UnityEditor.Editor
    {
        private SerializedProperty _fieldsProp;

        private static readonly string[] s_typeNames = { "Bool", "Int", "Float", "String" };

        private static readonly GUIStyle s_rowStyle = null; // lazy init

        private void OnEnable()
        {
            _fieldsProp = serializedObject.FindProperty("fields");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            // ---- Header ----
            EditorGUILayout.LabelField("Serialized ZScript Fields", EditorStyles.boldLabel);
            EditorGUILayout.HelpBox(
                "Values are injected into the ZScript instance before start() runs.\n" +
                "Field names must match ZScript field names exactly (case-sensitive).\n" +
                "Use @unity.serialize on the ZScript class to mark it as serializable.",
                MessageType.Info);

            EditorGUILayout.Space(4f);

            // ---- Column headers ----
            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.LabelField("Name",  EditorStyles.miniLabel, GUILayout.Width(130f));
            EditorGUILayout.LabelField("Type",  EditorStyles.miniLabel, GUILayout.Width(64f));
            EditorGUILayout.LabelField("Value", EditorStyles.miniLabel);
            GUILayout.Space(26f); // space for ✕ button
            EditorGUILayout.EndHorizontal();

            DrawSeparator();

            // ---- Field rows ----
            int removeAt = -1;

            for (int i = 0; i < _fieldsProp.arraySize; i++)
            {
                SerializedProperty elem    = _fieldsProp.GetArrayElementAtIndex(i);
                SerializedProperty nameProp  = elem.FindPropertyRelative("name");
                SerializedProperty typeProp  = elem.FindPropertyRelative("type");
                SerializedProperty boolProp  = elem.FindPropertyRelative("boolValue");
                SerializedProperty intProp   = elem.FindPropertyRelative("intValue");
                SerializedProperty floatProp = elem.FindPropertyRelative("floatValue");
                SerializedProperty strProp   = elem.FindPropertyRelative("stringValue");

                EditorGUILayout.BeginHorizontal();

                // Name field
                nameProp.stringValue = EditorGUILayout.TextField(
                    nameProp.stringValue, GUILayout.Width(130f));

                // Type dropdown
                typeProp.intValue = EditorGUILayout.Popup(
                    typeProp.intValue, s_typeNames, GUILayout.Width(64f));

                // Value field — adapts to selected type
                switch ((ZsFieldType)typeProp.intValue)
                {
                    case ZsFieldType.Bool:
                        boolProp.boolValue = EditorGUILayout.Toggle(
                            boolProp.boolValue, GUILayout.Width(20f));
                        GUILayout.FlexibleSpace();
                        break;

                    case ZsFieldType.Int:
                        intProp.intValue = EditorGUILayout.IntField(intProp.intValue);
                        break;

                    case ZsFieldType.Float:
                        floatProp.floatValue = EditorGUILayout.FloatField(floatProp.floatValue);
                        break;

                    case ZsFieldType.String:
                        strProp.stringValue =
                            EditorGUILayout.TextField(strProp.stringValue ?? string.Empty);
                        break;
                }

                // Remove button
                GUI.color = new Color(1f, 0.45f, 0.45f);
                if (GUILayout.Button("✕", GUILayout.Width(22f), GUILayout.Height(18f)))
                    removeAt = i;
                GUI.color = Color.white;

                EditorGUILayout.EndHorizontal();
            }

            // Deferred removal avoids modifying the array mid-iteration.
            if (removeAt >= 0)
                _fieldsProp.DeleteArrayElementAtIndex(removeAt);

            DrawSeparator();

            // ---- Add button ----
            EditorGUILayout.BeginHorizontal();
            GUILayout.FlexibleSpace();
            if (GUILayout.Button("+ Add Field", GUILayout.Width(100f)))
            {
                int idx = _fieldsProp.arraySize;
                _fieldsProp.InsertArrayElementAtIndex(idx);
                SerializedProperty newElem = _fieldsProp.GetArrayElementAtIndex(idx);
                newElem.FindPropertyRelative("name").stringValue        = "fieldName";
                newElem.FindPropertyRelative("type").intValue           = (int)ZsFieldType.Float;
                newElem.FindPropertyRelative("boolValue").boolValue     = false;
                newElem.FindPropertyRelative("intValue").intValue       = 0;
                newElem.FindPropertyRelative("floatValue").floatValue   = 0f;
                newElem.FindPropertyRelative("stringValue").stringValue = string.Empty;
            }
            EditorGUILayout.EndHorizontal();

            serializedObject.ApplyModifiedProperties();
        }

        private static void DrawSeparator()
        {
            Rect r = GUILayoutUtility.GetRect(0f, 1f, GUILayout.ExpandWidth(true));
            r.height = 1f;
            EditorGUI.DrawRect(r, new Color(0.3f, 0.3f, 0.3f, 0.5f));
            GUILayout.Space(2f);
        }
    }
}
