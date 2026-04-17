using NUnit.Framework;
using UnityEngine;
using System.Collections.Generic;

namespace ZScript.Tests
{
    public class TagsAndAnnotationTests
    {
        private GameObject _go;
        private ZScriptVM  _vm;

        [SetUp]
        public void SetUp()
        {
            _go = new GameObject("ZScriptVM_Annot");
            _vm = _go.AddComponent<ZScriptVM>();
            _vm.SendMessage("Awake", SendMessageOptions.DontRequireReceiver);
        }

        [TearDown]
        public void TearDown() => UnityEngine.Object.DestroyImmediate(_go);

        [Test]
        public void FindAnnotatedClasses_ReturnsClass_WithMatchingAnnotation()
        {
            const string src = @"
@component
class MyActor {
    fn update() { }
}
";
            _vm.LoadSource("<test>", src);
            string[] classes = _vm.FindAnnotatedClasses("", "component");
            Assert.IsNotNull(classes);
            Assert.IsTrue(classes.Length >= 1, "Expected at least one @component class");
            Assert.Contains("MyActor", classes);
        }

        [Test]
        public void FindAnnotatedClasses_ReturnsEmpty_WhenNoneMatch()
        {
            _vm.LoadSource("<test>", "class Plain { }");
            string[] classes = _vm.FindAnnotatedClasses("", "component");
            Assert.IsNotNull(classes);
            Assert.AreEqual(0, classes.Length);
        }

        [Test]
        public void GetClassAnnotations_ReturnsAnnotation_ForAnnotatedClass()
        {
            _vm.LoadSource("<test>", "@component class Tagged { }");
            string[] annotations = _vm.GetClassAnnotations("Tagged");
            Assert.IsNotNull(annotations);
            Assert.Contains("component", annotations);
        }

        [Test]
        public void GetClassAnnotations_ReturnsEmpty_ForUnannotatedClass()
        {
            _vm.LoadSource("<test>", "class Bare { }");
            string[] annotations = _vm.GetClassAnnotations("Bare");
            Assert.IsNotNull(annotations);
            Assert.AreEqual(0, annotations.Length);
        }

        [Test]
        public void GetComponentClasses_FindsComponentAnnotatedClass()
        {
            // GetComponentClasses searches for @unity.component (ns="unity", name="component")
            _vm.LoadSource("<test>", "@unity.component class Actor { fn start() { } }");
            string[] classes = _vm.GetComponentClasses();
            Assert.IsNotNull(classes);
            Assert.Contains("Actor", classes);
        }
    }
}
