using NUnit.Framework;
using UnityEngine;
using System;

namespace ZScript.Tests
{
    /// <summary>
    /// EditMode tests for ZScript VM lifecycle, script loading, and value marshalling.
    /// These run in Unity's test runner without entering Play Mode.
    /// </summary>
    public class VMLifecycleTests
    {
        private GameObject _go;
        private ZScriptVM  _vm;

        [SetUp]
        public void SetUp()
        {
            _go = new GameObject("ZScriptVM_Test");
            _vm = _go.AddComponent<ZScriptVM>();
            // Awake is not called automatically in EditMode — invoke manually.
            _vm.Awake();
        }

        [TearDown]
        public void TearDown()
        {
            UnityEngine.Object.DestroyImmediate(_go);
        }

        // ------------------------------------------------------------------ //
        // VM lifecycle
        // ------------------------------------------------------------------ //

        [Test]
        public void VM_IsCreated_AfterAwake()
        {
            Assert.IsNotNull(_vm, "ZScriptVM component must exist");
            Assert.AreNotEqual(IntPtr.Zero, _vm.RawVM, "Native VM handle must be non-null after Awake");
        }

        // ------------------------------------------------------------------ //
        // Script loading and function calls
        // ------------------------------------------------------------------ //

        [Test]
        public void LoadSource_SimpleFunction_ReturnsExpectedValue()
        {
            const string src = "fn add(a, b) { return a + b; }";
            bool loaded = _vm.LoadSource("<test>", src);
            Assert.IsTrue(loaded, "Script should compile without errors");

            using var a = ZsValueHandle.FromFloat(3);
            using var b = ZsValueHandle.FromFloat(4);
            using var result = _vm.Call("add", a, b);
            Assert.IsNotNull(result, "Call must return a result");
            Assert.AreEqual(7.0, result.AsFloat(), 1e-9);
        }

        [Test]
        public void LoadSource_SyntaxError_ReturnsFalse()
        {
            const string src = "fn broken( { }";
            bool loaded = _vm.LoadSource("<bad>", src);
            Assert.IsFalse(loaded, "Syntax error must cause LoadSource to return false");
        }

        [Test]
        public void Call_UndefinedFunction_ReturnsNull()
        {
            var result = _vm.Call("nonexistent");
            Assert.IsNull(result, "Calling undefined function must return null");
        }

        // ------------------------------------------------------------------ //
        // Value marshalling
        // ------------------------------------------------------------------ //

        [Test]
        public void Call_ReturnsBool_True()
        {
            _vm.LoadSource("<test>", "fn yes() { return true; }");
            using var v = _vm.Call("yes");
            Assert.IsNotNull(v);
            Assert.IsTrue(v.AsBool());
        }

        [Test]
        public void Call_ReturnsString()
        {
            _vm.LoadSource("<test>", "fn greet() { return \"hello\"; }");
            using var v = _vm.Call("greet");
            Assert.IsNotNull(v);
            Assert.AreEqual("hello", v.AsString());
        }

        [Test]
        public void Call_ReturnsNil_WhenNoReturn()
        {
            _vm.LoadSource("<test>", "fn noop() { }");
            using var v = _vm.Call("noop");
            // A function with no explicit return yields nil.
            bool isNil = v == null || v.IsInvalid || v.Type == ZsType.Nil;
            Assert.IsTrue(isNil, "No-return function must yield nil");
        }

        // ------------------------------------------------------------------ //
        // Error handling
        // ------------------------------------------------------------------ //

        [Test]
        public void Call_RuntimeError_DoesNotThrow()
        {
            _vm.LoadSource("<test>", "fn boom() { return 1 / 0; }");
            Assert.DoesNotThrow(() => { using var v = _vm.Call("boom"); },
                "Runtime errors must be caught internally and not propagate as C# exceptions");
        }
    }
}
