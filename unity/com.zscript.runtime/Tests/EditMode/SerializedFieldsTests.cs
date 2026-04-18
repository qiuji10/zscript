// SerializedFieldsTests.cs — EditMode tests for ZsSerializedFields.Apply().
//
// Verifies that each supported field type is correctly written into a ZScript
// table instance via zs_table_set_value, and that edge-cases (empty list,
// null/invalid instance) are handled without exceptions.
using System;
using NUnit.Framework;
using UnityEngine;

namespace ZScript.Tests
{
    public class SerializedFieldsTests
    {
        private GameObject  _vmGo;
        private ZScriptVM   _vm;

        // A helper ZScript function that reads any key from a table by index.
        // ZScript supports tbl[key] for string-keyed table access.
        private const string ReadFieldSrc =
            "fn readField(tbl, key) { return tbl[key] }";

        [SetUp]
        public void SetUp()
        {
            _vmGo = new GameObject("ZScriptVM_SF");
            _vm   = _vmGo.AddComponent<ZScriptVM>();
            _vm.Awake();
            _vm.LoadSource("<helpers>", ReadFieldSrc);
        }

        [TearDown]
        public void TearDown() => UnityEngine.Object.DestroyImmediate(_vmGo);

        // ----------------------------------------------------------------
        // Helper — create a fresh ZScript table instance
        // ----------------------------------------------------------------
        private ZsValueHandle MakeTable() => new ZsValueHandle(ZsNative.zs_table_new());

        // Helper — read a field from a ZScript table via the readField() function
        private ZsValueHandle ReadField(ZsValueHandle tbl, string key)
        {
            using var keyVal = ZsValueHandle.FromString(key);
            return _vm.Call("readField", tbl, keyVal);
        }

        // Helper — create a ZsSerializedFields component on a temporary GameObject
        private (GameObject go, ZsSerializedFields sf) MakeSf()
        {
            var go = new GameObject("SF");
            var sf = go.AddComponent<ZsSerializedFields>();
            return (go, sf);
        }

        // ----------------------------------------------------------------
        // Bool
        // ----------------------------------------------------------------

        [Test]
        public void Apply_SetsBoolField_True()
        {
            using var tbl = MakeTable();
            var (go, sf) = MakeSf();
            sf.SetField("active", ZsFieldType.Bool, bVal: true);
            sf.Apply(tbl);
            UnityEngine.Object.DestroyImmediate(go);

            using var result = ReadField(tbl, "active");
            Assert.IsNotNull(result, "readField must return a value");
            Assert.IsTrue(result.AsBool(), "bool field 'active' must be true");
        }

        [Test]
        public void Apply_SetsBoolField_False()
        {
            using var tbl = MakeTable();
            var (go, sf) = MakeSf();
            sf.SetField("visible", ZsFieldType.Bool, bVal: false);
            sf.Apply(tbl);
            UnityEngine.Object.DestroyImmediate(go);

            using var result = ReadField(tbl, "visible");
            Assert.IsNotNull(result);
            Assert.IsFalse(result.AsBool(), "bool field 'visible' must be false");
        }

        // ----------------------------------------------------------------
        // Int
        // ----------------------------------------------------------------

        [Test]
        public void Apply_SetsIntField()
        {
            using var tbl = MakeTable();
            var (go, sf) = MakeSf();
            sf.SetField("lives", ZsFieldType.Int, iVal: 42);
            sf.Apply(tbl);
            UnityEngine.Object.DestroyImmediate(go);

            using var result = ReadField(tbl, "lives");
            Assert.IsNotNull(result);
            Assert.AreEqual(42L, result.AsInt(), "int field 'lives' must be 42");
        }

        // ----------------------------------------------------------------
        // Float
        // ----------------------------------------------------------------

        [Test]
        public void Apply_SetsFloatField()
        {
            using var tbl = MakeTable();
            var (go, sf) = MakeSf();
            sf.SetField("speed", ZsFieldType.Float, fVal: 3.14f);
            sf.Apply(tbl);
            UnityEngine.Object.DestroyImmediate(go);

            using var result = ReadField(tbl, "speed");
            Assert.IsNotNull(result);
            Assert.AreEqual(3.14f, (float)result.AsFloat(), 0.0001f,
                "float field 'speed' must equal 3.14");
        }

        // ----------------------------------------------------------------
        // String
        // ----------------------------------------------------------------

        [Test]
        public void Apply_SetsStringField()
        {
            using var tbl = MakeTable();
            var (go, sf) = MakeSf();
            sf.SetField("tag", ZsFieldType.String, sVal: "enemy");
            sf.Apply(tbl);
            UnityEngine.Object.DestroyImmediate(go);

            using var result = ReadField(tbl, "tag");
            Assert.IsNotNull(result);
            Assert.AreEqual("enemy", result.AsString(), "string field 'tag' must equal 'enemy'");
        }

        [Test]
        public void Apply_SetsStringField_Empty()
        {
            using var tbl = MakeTable();
            var (go, sf) = MakeSf();
            sf.SetField("label", ZsFieldType.String, sVal: "");
            sf.Apply(tbl);
            UnityEngine.Object.DestroyImmediate(go);

            using var result = ReadField(tbl, "label");
            Assert.IsNotNull(result);
            Assert.AreEqual("", result.AsString(), "empty string field must round-trip as empty");
        }

        // ----------------------------------------------------------------
        // Multiple fields
        // ----------------------------------------------------------------

        [Test]
        public void Apply_SetsMultipleFields()
        {
            using var tbl = MakeTable();
            var (go, sf) = MakeSf();
            sf.SetField("hp",    ZsFieldType.Int,   iVal: 100);
            sf.SetField("speed", ZsFieldType.Float, fVal: 5.5f);
            sf.SetField("name",  ZsFieldType.String, sVal: "goblin");
            sf.Apply(tbl);
            UnityEngine.Object.DestroyImmediate(go);

            using var hp    = ReadField(tbl, "hp");
            using var speed = ReadField(tbl, "speed");
            using var name  = ReadField(tbl, "name");

            Assert.AreEqual(100L,      hp.AsInt());
            Assert.AreEqual(5.5f,      (float)speed.AsFloat(), 0.0001f);
            Assert.AreEqual("goblin",  name.AsString());
        }

        // ----------------------------------------------------------------
        // SetField overwrites existing entry
        // ----------------------------------------------------------------

        [Test]
        public void SetField_Overwrites_ExistingEntry()
        {
            var (go, sf) = MakeSf();
            sf.SetField("hp", ZsFieldType.Int, iVal: 50);
            sf.SetField("hp", ZsFieldType.Int, iVal: 99); // overwrite
            Assert.AreEqual(1, sf.fields.Count, "SetField must not duplicate entries");
            Assert.AreEqual(99, sf.fields[0].intValue);
            UnityEngine.Object.DestroyImmediate(go);
        }

        // ----------------------------------------------------------------
        // Edge cases
        // ----------------------------------------------------------------

        [Test]
        public void Apply_EmptyFieldList_DoesNotThrow()
        {
            using var tbl = MakeTable();
            var (go, sf) = MakeSf();
            // no fields added
            Assert.DoesNotThrow(() => sf.Apply(tbl), "Apply() on empty list must not throw");
            UnityEngine.Object.DestroyImmediate(go);
        }

        [Test]
        public void Apply_NullInstance_DoesNotThrow()
        {
            var (go, sf) = MakeSf();
            sf.SetField("hp", ZsFieldType.Int, iVal: 5);
            Assert.DoesNotThrow(() => sf.Apply(null), "Apply(null) must not throw");
            UnityEngine.Object.DestroyImmediate(go);
        }

        [Test]
        public void Apply_SkipsEntries_WithEmptyName()
        {
            using var tbl = MakeTable();
            var (go, sf) = MakeSf();
            // Manually add a field with an empty name (simulates Inspector leaving name blank)
            sf.fields.Add(new ZsFieldEntry { name = "", type = ZsFieldType.Int, intValue = 7 });
            Assert.DoesNotThrow(() => sf.Apply(tbl),
                "Apply() must silently skip entries with empty names");
            UnityEngine.Object.DestroyImmediate(go);
        }

        [Test]
        public void ClearFields_RemovesAllEntries()
        {
            var (go, sf) = MakeSf();
            sf.SetField("a", ZsFieldType.Int, iVal: 1);
            sf.SetField("b", ZsFieldType.Float, fVal: 2f);
            sf.ClearFields();
            Assert.AreEqual(0, sf.fields.Count, "ClearFields must remove all entries");
            UnityEngine.Object.DestroyImmediate(go);
        }
    }
}
