// ZsCoroutineBridge.cs — Wraps a ZScript coroutine in a Unity IEnumerator.
//
// Usage from C# (via ZScriptVM):
//   Coroutine handle = vm.StartZsCoroutine("myRoutine");
//
// Usage from ZScript (after RegisterYieldHelpers() is called):
//   fn myRoutine() {
//       coroutine.yield(WaitForSeconds(1.5))
//       coroutine.yield(WaitForEndOfFrame())
//       coroutine.yield(WaitForFixedUpdate())
//       coroutine.yield(WaitUntil(fn() { return ready }))
//       coroutine.yield(WaitWhile(fn() { return !done }))
//   }
//
// Yield value convention — ZScript yields a table with a "__yield_type" string:
//   { __yield_type: "WaitForSeconds",    seconds: <float>  }
//   { __yield_type: "WaitForEndOfFrame"                    }
//   { __yield_type: "WaitForFixedUpdate"                   }
//   { __yield_type: "WaitUntil",         fn: <closure>     }
//   { __yield_type: "WaitWhile",         fn: <closure>     }
// Any other yielded value (nil, number, …) → one-frame wait (Current = null).
using System;
using System.Collections;
using UnityEngine;

namespace ZScript
{
    /// <summary>
    /// Unity IEnumerator that drives a ZScript coroutine.
    /// Each MoveNext() resumes the coroutine once and translates the yielded
    /// value into a Unity YieldInstruction.
    /// </summary>
    public sealed class ZsCoroutineBridge : IEnumerator
    {
        private readonly IntPtr        _vm;
        private readonly ZsValueHandle _co;   // owned — freed when bridge is GC'd
        private object                 _current;
        private bool                   _done;

        /// <param name="vm">Raw VM pointer (ZScriptVM.RawVM).</param>
        /// <param name="coVal">
        ///   ZsValueHandle for the ZScript coroutine. Ownership is transferred here.
        /// </param>
        public ZsCoroutineBridge(IntPtr vm, ZsValueHandle coVal)
        {
            _vm   = vm;
            _co   = coVal;
            _done = false;
        }

        // ----------------------------------------------------------------
        // IEnumerator
        // ----------------------------------------------------------------
        public object Current => _current;

        public bool MoveNext()
        {
            if (_done) return false;

            IntPtr outRaw;
            int status = ZsNative.zs_coroutine_resume(
                _vm, _co.Raw,
                0, Array.Empty<IntPtr>(),
                out outRaw);

            if (status == -1)
            {
                // Not a coroutine.
                _done = true;
                return false;
            }

            // Take ownership of the yielded value.
            var yielded = new ZsValueHandle(outRaw);

            if (status == 0)
            {
                // Coroutine finished (returned normally or errored).
                yielded.Dispose();
                _done = true;
                return false;
            }

            // status == 1 → yielded; translate to Unity yield instruction.
            _current = TranslateYield(yielded);
            // yielded is NOT disposed here — TranslateYield may capture it (WaitUntil/WaitWhile).
            // For non-capturing cases (WaitForSeconds etc.) yielded is disposed inside.
            return true;
        }

        public void Reset() =>
            throw new NotSupportedException("ZsCoroutineBridge does not support Reset().");

        // ----------------------------------------------------------------
        // Yield translation
        // ----------------------------------------------------------------
        private object TranslateYield(ZsValueHandle val)
        {
            if (val.IsInvalid || val.Type != ZsType.Table)
            {
                val.Dispose();
                return null; // one-frame wait
            }

            string yieldType = GetStringField(val, "__yield_type");
            if (yieldType == null)
            {
                val.Dispose();
                return null;
            }

            switch (yieldType)
            {
                case "WaitForSeconds":
                {
                    float seconds = (float)GetFloatField(val, "seconds");
                    val.Dispose();
                    return new WaitForSeconds(seconds);
                }

                case "WaitForEndOfFrame":
                    val.Dispose();
                    return new WaitForEndOfFrame();

                case "WaitForFixedUpdate":
                    val.Dispose();
                    return new WaitForFixedUpdate();

                case "WaitUntil":
                {
                    // Extract the predicate closure.  fnHandle takes ownership of
                    // the native ZsValue; the lambda keeps fnHandle alive until
                    // Unity's scheduler discards the WaitUntil, after which it is
                    // collected and freed by the SafeHandle finalizer.
                    ZsValueHandle fnHandle = ExtractField(val, "fn");
                    val.Dispose();
                    return new WaitUntil(() => CallPredicateClosure(fnHandle));
                }

                case "WaitWhile":
                {
                    ZsValueHandle fnHandle = ExtractField(val, "fn");
                    val.Dispose();
                    return new WaitWhile(() => CallPredicateClosure(fnHandle));
                }

                default:
                    val.Dispose();
                    return null;
            }
        }

        // ----------------------------------------------------------------
        // Field accessors — read from a plain ZScript table value
        // ----------------------------------------------------------------
        // zs_vm_handle_get_field works on any table (not just object proxies).
        // It always returns a non-null ZsValueBox*, holding nil when key absent.
        private static string GetStringField(ZsValueHandle tbl, string key)
        {
            using var fv = new ZsValueHandle(
                ZsNative.zs_vm_handle_get_field(IntPtr.Zero, tbl.Raw, key));
            return fv.Type == ZsType.String ? fv.AsString() : null;
        }

        private static double GetFloatField(ZsValueHandle tbl, string key)
        {
            using var fv = new ZsValueHandle(
                ZsNative.zs_vm_handle_get_field(IntPtr.Zero, tbl.Raw, key));
            return fv.AsFloat();
        }

        // Extract a field as an owned ZsValueHandle (caller must dispose).
        private static ZsValueHandle ExtractField(ZsValueHandle tbl, string key)
            => new ZsValueHandle(ZsNative.zs_vm_handle_get_field(IntPtr.Zero, tbl.Raw, key));

        // ----------------------------------------------------------------
        // Call a ZScript closure (no args) and return its bool result.
        // Used for WaitUntil / WaitWhile predicates.
        // We wrap the closure in a one-shot coroutine so we can call it
        // through the existing coroutine resume path without adding a new
        // C API entry point.
        // ----------------------------------------------------------------
        private bool CallPredicateClosure(ZsValueHandle fnHandle)
        {
            if (fnHandle.IsInvalid) return true; // fail-safe: stop waiting

            // Wrap the closure in a one-shot coroutine and resume it once.
            // A plain predicate function doesn't yield, so it runs to completion
            // and returns its bool result as the resume output value.
            using var co = new ZsValueHandle(ZsNative.zs_coroutine_create(_vm, fnHandle.Raw));
            if (co.Type != ZsType.Coroutine) return true;

            IntPtr outRaw;
            // status 0 = dead (function returned), which is expected.
            ZsNative.zs_coroutine_resume(_vm, co.Raw, 0, Array.Empty<IntPtr>(), out outRaw);

            using var result = new ZsValueHandle(outRaw);
            return result.AsBool();
        }
    }
}
