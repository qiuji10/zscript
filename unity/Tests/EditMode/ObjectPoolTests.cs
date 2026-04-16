using NUnit.Framework;
using UnityEngine;

namespace ZScript.Tests
{
    public class ObjectPoolTests
    {
        private ZsObjectPool _pool;

        [SetUp]
        public void SetUp() => _pool = new ZsObjectPool();

        [TearDown]
        public void TearDown() => _pool = null;

        [Test]
        public void Alloc_ReturnsPositiveId()
        {
            var go = new GameObject("pool_test");
            long id = _pool.Alloc(go);
            Assert.Greater(id, 0L);
            UnityEngine.Object.DestroyImmediate(go);
        }

        [Test]
        public void IsValid_ReturnsTrueForAllocated()
        {
            var go = new GameObject("pool_valid");
            long id = _pool.Alloc(go);
            Assert.IsTrue(_pool.IsValid(id));
            UnityEngine.Object.DestroyImmediate(go);
        }

        [Test]
        public void IsValid_ReturnsFalseAfterInvalidate()
        {
            var go = new GameObject("pool_invalid");
            long id = _pool.Alloc(go);
            _pool.Invalidate(id);
            Assert.IsFalse(_pool.IsValid(id));
            UnityEngine.Object.DestroyImmediate(go);
        }

        [Test]
        public void Get_ReturnsCorrectObject()
        {
            var go = new GameObject("pool_get");
            long id = _pool.Alloc(go);
            var retrieved = _pool.Get<GameObject>(id);
            Assert.AreSame(go, retrieved);
            UnityEngine.Object.DestroyImmediate(go);
        }

        [Test]
        public void Get_ReturnsNull_ForInvalidId()
        {
            var result = _pool.Get<GameObject>(999999L);
            Assert.IsNull(result);
        }

        [Test]
        public void UniqueIds_AreAssigned_ForMultipleAllocs()
        {
            var go1 = new GameObject("p1");
            var go2 = new GameObject("p2");
            long id1 = _pool.Alloc(go1);
            long id2 = _pool.Alloc(go2);
            Assert.AreNotEqual(id1, id2);
            UnityEngine.Object.DestroyImmediate(go1);
            UnityEngine.Object.DestroyImmediate(go2);
        }
    }
}
