// ZsSerializedFields.cs — Unity-serialized field store for @unity.serialize ZScript classes.
//
// Attach alongside ZsComponentBridge on the same GameObject to give Unity's Inspector
// control over fields that live inside a ZScript class instance.
//
// Workflow:
//   1. Annotate your ZScript class: @unity.serialize class Enemy { var hp = 100 }
//   2. Attach ZsComponentBridge + ZsSerializedFields to a GameObject.
//   3. In the Inspector, add a "hp" field of type Int and set it.
//   4. ZsComponentBridge.Start() calls Apply() before invoking the ZScript start()
//      method, so the values arrive before any script logic runs.
//
// Field names must match the ZScript field names exactly (case-sensitive).
using System;
using System.Collections.Generic;
using UnityEngine;

namespace ZScript
{
    // -----------------------------------------------------------------------
    // Data types
    // -----------------------------------------------------------------------

    /// <summary>Primitive type of a serialized ZScript field.</summary>
    public enum ZsFieldType
    {
        Bool   = 0,
        Int    = 1,
        Float  = 2,
        String = 3,
    }

    /// <summary>
    /// A single serialized field — name, type, and one value slot per supported type.
    /// Unity serializes all value slots; only the slot that matches <see cref="type"/>
    /// is used at runtime.
    /// </summary>
    [Serializable]
    public struct ZsFieldEntry
    {
        public string     name;
        public ZsFieldType type;
        public bool       boolValue;
        public int        intValue;
        public float      floatValue;
        public string     stringValue;
    }

    // -----------------------------------------------------------------------
    // Component
    // -----------------------------------------------------------------------

    /// <summary>
    /// Stores Unity-inspector-editable field values for a <c>@unity.serialize</c>
    /// ZScript class. <see cref="ZsComponentBridge"/> reads and injects these
    /// values into the ZScript instance via <see cref="Apply"/> before calling
    /// the ZScript <c>start()</c> lifecycle method.
    /// </summary>
    [AddComponentMenu("ZScript/ZScript Serialized Fields")]
    [DisallowMultipleComponent]
    public sealed class ZsSerializedFields : MonoBehaviour
    {
        [SerializeField]
        internal List<ZsFieldEntry> fields = new List<ZsFieldEntry>();

        // ----------------------------------------------------------------
        // Runtime
        // ----------------------------------------------------------------

        /// <summary>
        /// Write all stored fields into the ZScript table <paramref name="instance"/>
        /// using <c>zs_table_set_value</c>. Silently skips entries whose name is
        /// null or empty. Safe to call with a null or invalid handle.
        /// </summary>
        public void Apply(ZsValueHandle instance)
        {
            if (instance == null || instance.IsInvalid) return;
            foreach (var f in fields)
            {
                if (string.IsNullOrEmpty(f.name)) continue;
                using var val = f.type switch
                {
                    ZsFieldType.Bool   => ZsValueHandle.FromBool(f.boolValue),
                    ZsFieldType.Int    => ZsValueHandle.FromInt(f.intValue),
                    ZsFieldType.Float  => ZsValueHandle.FromFloat(f.floatValue),
                    ZsFieldType.String => ZsValueHandle.FromString(f.stringValue ?? string.Empty),
                    _                  => ZsValueHandle.Nil(),
                };
                ZsNative.zs_table_set_value(instance.Raw, f.name, val.Raw);
            }
        }

        // ----------------------------------------------------------------
        // Editor / test helpers (internal — visible to ZScript.Editor and tests)
        // ----------------------------------------------------------------

        /// <summary>
        /// Add or overwrite a field entry by name. Useful from Editor scripts and tests.
        /// </summary>
        internal void SetField(string fieldName, ZsFieldType type,
                               bool   bVal = false,
                               int    iVal = 0,
                               float  fVal = 0f,
                               string sVal = "")
        {
            for (int i = 0; i < fields.Count; i++)
            {
                if (fields[i].name != fieldName) continue;
                fields[i] = MakeEntry(fieldName, type, bVal, iVal, fVal, sVal);
                return;
            }
            fields.Add(MakeEntry(fieldName, type, bVal, iVal, fVal, sVal));
        }

        /// <summary>Remove all field entries. Useful for resetting state in tests.</summary>
        internal void ClearFields() => fields.Clear();

        private static ZsFieldEntry MakeEntry(string name, ZsFieldType type,
                                              bool b, int i, float f, string s)
            => new ZsFieldEntry
               {
                   name        = name,
                   type        = type,
                   boolValue   = b,
                   intValue    = i,
                   floatValue  = f,
                   stringValue = s,
               };
    }
}
