#if ZSCRIPT_UGUI
// ZsUGUIBindings.cs — ZScript bindings for Unity UGUI components.
// Registers a global "UGUI" table with factory functions that wrap UI components.
//
// Usage (ZScript):
//   var btn = UGUI.GetButton(myGameObject)
//   local id = btn.onClick.AddListener(fn() { Debug.Log("clicked!") })
//   btn.interactable = false
//
//   var slider = UGUI.GetSlider(myGameObject)
//   slider.onValueChanged.AddListener(fn(v) { Debug.Log(v) })
//   slider.value = 0.5
//
// Factory functions (each takes a GameObject handle):
//   UGUI.GetButton(go)      → proxy: onClick event, interactable
//   UGUI.GetToggle(go)      → proxy: onValueChanged event, isOn, interactable
//   UGUI.GetSlider(go)      → proxy: onValueChanged event, value, minValue, maxValue
//   UGUI.GetInputField(go)  → proxy: onValueChanged, onEndEdit events, text, isFocused
//   UGUI.GetText(go)        → proxy: text, fontSize, color
//   UGUI.GetImage(go)       → proxy: color, fillAmount
//   UGUI.GetScrollRect(go)  → proxy: onValueChanged event, normalizedPosition
//
// Event proxy lifetime:
//   Event proxies (e.g. btn.onClick) are cached per-component-instance so that
//   AddListener / RemoveListener ids are coherent across multiple accesses.
//   Proxies are disposed when the ZScript VM is torn down.
//
using System;
using System.Collections.Generic;
using System.Text;
using UnityEngine;
using UnityEngine.UI;

namespace ZScript
{
    internal static class ZsUGUIBindings
    {
        // Keeps all ZsNativeFn delegates alive so the GC can't collect them while
        // the C++ ZScript VM holds raw function pointers into managed memory.
        private static ZsNativeFn[] _pins;

        public static void Register(ZScriptVM vm)
        {
            IntPtr rawVm     = vm.RawVM;
            ZsObjectPool pool = vm.ObjectPool;
            var fns = new List<ZsNativeFn>();
            ZsNativeFn pin(ZsNativeFn fn) { fns.Add(fn); return fn; }

            // ── string helper ─────────────────────────────────────────────────
            string zsStr(IntPtr v)
            {
                int len = ZsNative.zs_value_as_string(v, null, 0);
                byte[] buf = new byte[len + 1];
                ZsNative.zs_value_as_string(v, buf, buf.Length);
                return Encoding.UTF8.GetString(buf, 0, len);
            }

            // ── Table field helpers (works for plain {x,y,z} tables) ───────────
            float fld(IntPtr tbl, string key) =>
                (float)ZsNative.zs_value_as_float(
                    ZsNative.zs_vm_handle_get_field(IntPtr.Zero, tbl, key));

            // ── Color helpers ─────────────────────────────────────────────────
            Color readColor(IntPtr v) =>
                new Color(fld(v, "r"), fld(v, "g"), fld(v, "b"),
                    ZsNative.zs_value_type(v) == ZsType.Table ? fld(v, "a") : 1f);

            IntPtr writeColor(Color c)
            {
                IntPtr tbl = ZsNative.zs_table_new();
                void setF(string n, float val) {
                    IntPtr fv = ZsNative.zs_value_float(val);
                    ZsNative.zs_table_set_value(tbl, n, fv);
                    ZsNative.zs_value_free(fv);
                }
                setF("r", c.r); setF("g", c.g); setF("b", c.b); setF("a", c.a);
                return tbl;
            }

            // ── Component lookup from a component object handle ───────────────
            // argv[0] in __index/__newindex is the component handle itself;
            // the pool maps that handle's id directly to the component object.
            // Returns null if the handle is invalid or the object was destroyed.
            T getComp<T>(IntPtr handleVal) where T : Component
            {
                long id = ZsNative.zs_vm_get_object_handle(rawVm, handleVal);
                if (id < 0 || !pool.IsValid(id)) return null;
                return pool.Get<T>(id);
            }

            // ── Component proxy factory ───────────────────────────────────────
            // Gets component T from the GO represented by goHandleVal, allocs it
            // in the pool, creates an object handle, and wires __index/__newindex.
            IntPtr makeProxy<T>(IntPtr goHandleVal, ZsNativeFn idxFn, ZsNativeFn newidxFn)
                where T : Component
            {
                long goId = ZsNative.zs_vm_get_object_handle(rawVm, goHandleVal);
                var go = goId >= 0 ? pool.Get<GameObject>(goId) : null;
                if (go == null) return ZsNative.zs_value_nil();
                var comp = go.GetComponent<T>();
                if (comp == null) return ZsNative.zs_value_nil();
                long compId = pool.Alloc(comp);
                IntPtr compHandle = ZsNative.zs_vm_push_object_handle(rawVm, compId);
                ZsNative.zs_vm_handle_set_index(rawVm, compHandle, idxFn);
                ZsNative.zs_vm_handle_set_newindex(rawVm, compHandle, newidxFn);
                return compHandle;
            }

            // ── Cached event proxy helper ─────────────────────────────────────
            // Returns a cloned ZsValue pointing at the cached proxy ZTable.
            // If no proxy exists for this component instance yet, creates one.
            IntPtr getOrCreateEventProxy<TEvent>(
                Dictionary<int, (TEvent ev, ZsValueHandle proxy)> cache,
                int instanceId,
                Func<TEvent> createFn,
                Func<TEvent, ZsValueHandle> proxyFn)
                where TEvent : IDisposable
            {
                if (!cache.TryGetValue(instanceId, out var entry))
                {
                    var ev = createFn();
                    entry = (ev, proxyFn(ev));
                    cache[instanceId] = entry;
                }
                return ZsNative.zs_value_clone(entry.proxy.Raw);
            }

            // ================================================================
            // Button
            // ================================================================
            var buttonClickEvents = new Dictionary<int, (ZsUnityEvent ev, ZsValueHandle proxy)>();

            ZsNativeFn buttonIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 2) return ZsNative.zs_value_nil();
                var btn = getComp<Button>(argv[0]);
                if (btn == null) return ZsNative.zs_value_nil();
                return zsStr(argv[1]) switch
                {
                    "onClick"      => getOrCreateEventProxy(
                                        buttonClickEvents, btn.GetInstanceID(),
                                        () => new ZsUnityEvent(rawVm, btn.onClick),
                                        ev => ev.CreateProxy()),
                    "interactable" => ZsNative.zs_value_bool(btn.interactable ? 1 : 0),
                    _              => ZsNative.zs_value_nil()
                };
            });

            ZsNativeFn buttonNewIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 3) return ZsNative.zs_value_nil();
                var btn = getComp<Button>(argv[0]);
                if (btn != null && zsStr(argv[1]) == "interactable")
                    btn.interactable = ZsNative.zs_value_as_bool(argv[2]) != 0;
                return ZsNative.zs_value_nil();
            });

            // ================================================================
            // Toggle
            // ================================================================
            var toggleValueEvents = new Dictionary<int, (ZsUnityEvent<bool> ev, ZsValueHandle proxy)>();

            ZsNativeFn toggleIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 2) return ZsNative.zs_value_nil();
                var tog = getComp<Toggle>(argv[0]);
                if (tog == null) return ZsNative.zs_value_nil();
                return zsStr(argv[1]) switch
                {
                    "onValueChanged" => getOrCreateEventProxy(
                                         toggleValueEvents, tog.GetInstanceID(),
                                         () => new ZsUnityEvent<bool>(rawVm, tog.onValueChanged, ZsMarshal.Bool),
                                         ev => ev.CreateProxy()),
                    "isOn"           => ZsNative.zs_value_bool(tog.isOn ? 1 : 0),
                    "interactable"   => ZsNative.zs_value_bool(tog.interactable ? 1 : 0),
                    _                => ZsNative.zs_value_nil()
                };
            });

            ZsNativeFn toggleNewIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 3) return ZsNative.zs_value_nil();
                var tog = getComp<Toggle>(argv[0]);
                if (tog == null) return ZsNative.zs_value_nil();
                bool bv = ZsNative.zs_value_as_bool(argv[2]) != 0;
                string key = zsStr(argv[1]);
                if      (key == "isOn")        tog.isOn        = bv;
                else if (key == "interactable") tog.interactable = bv;
                return ZsNative.zs_value_nil();
            });

            // ================================================================
            // Slider
            // ================================================================
            var sliderValueEvents = new Dictionary<int, (ZsUnityEvent<float> ev, ZsValueHandle proxy)>();

            ZsNativeFn sliderIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 2) return ZsNative.zs_value_nil();
                var sl = getComp<Slider>(argv[0]);
                if (sl == null) return ZsNative.zs_value_nil();
                return zsStr(argv[1]) switch
                {
                    "onValueChanged"  => getOrCreateEventProxy(
                                          sliderValueEvents, sl.GetInstanceID(),
                                          () => new ZsUnityEvent<float>(rawVm, sl.onValueChanged, ZsMarshal.Float),
                                          ev => ev.CreateProxy()),
                    "value"           => ZsNative.zs_value_float(sl.value),
                    "minValue"        => ZsNative.zs_value_float(sl.minValue),
                    "maxValue"        => ZsNative.zs_value_float(sl.maxValue),
                    "normalizedValue" => ZsNative.zs_value_float(sl.normalizedValue),
                    "wholeNumbers"    => ZsNative.zs_value_bool(sl.wholeNumbers ? 1 : 0),
                    _                 => ZsNative.zs_value_nil()
                };
            });

            ZsNativeFn sliderNewIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 3) return ZsNative.zs_value_nil();
                var sl = getComp<Slider>(argv[0]);
                if (sl == null) return ZsNative.zs_value_nil();
                string key = zsStr(argv[1]);
                float fv   = (float)ZsNative.zs_value_as_float(argv[2]);
                if      (key == "value")        sl.value        = fv;
                else if (key == "minValue")     sl.minValue     = fv;
                else if (key == "maxValue")     sl.maxValue     = fv;
                else if (key == "wholeNumbers") sl.wholeNumbers = ZsNative.zs_value_as_bool(argv[2]) != 0;
                return ZsNative.zs_value_nil();
            });

            // ================================================================
            // InputField
            // ================================================================
            var inputValueEvents = new Dictionary<int, (ZsUnityEvent<string> ev, ZsValueHandle proxy)>();
            var inputEndEvents   = new Dictionary<int, (ZsUnityEvent<string> ev, ZsValueHandle proxy)>();

            ZsNativeFn inputFieldIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 2) return ZsNative.zs_value_nil();
                var field = getComp<InputField>(argv[0]);
                if (field == null) return ZsNative.zs_value_nil();
                return zsStr(argv[1]) switch
                {
                    "onValueChanged" => getOrCreateEventProxy(
                                         inputValueEvents, field.GetInstanceID(),
                                         () => new ZsUnityEvent<string>(rawVm, field.onValueChanged, ZsMarshal.String),
                                         ev => ev.CreateProxy()),
                    "onEndEdit"      => getOrCreateEventProxy(
                                         inputEndEvents, field.GetInstanceID(),
                                         () => new ZsUnityEvent<string>(rawVm, field.onEndEdit, ZsMarshal.String),
                                         ev => ev.CreateProxy()),
                    "text"           => ZsNative.zs_value_string(field.text ?? ""),
                    "isFocused"      => ZsNative.zs_value_bool(field.isFocused ? 1 : 0),
                    "interactable"   => ZsNative.zs_value_bool(field.interactable ? 1 : 0),
                    _                => ZsNative.zs_value_nil()
                };
            });

            ZsNativeFn inputFieldNewIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 3) return ZsNative.zs_value_nil();
                var field = getComp<InputField>(argv[0]);
                if (field == null) return ZsNative.zs_value_nil();
                string key = zsStr(argv[1]);
                if      (key == "text")         field.text         = zsStr(argv[2]);
                else if (key == "interactable")  field.interactable = ZsNative.zs_value_as_bool(argv[2]) != 0;
                return ZsNative.zs_value_nil();
            });

            // ================================================================
            // Text (legacy UnityEngine.UI.Text)
            // ================================================================
            ZsNativeFn textIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 2) return ZsNative.zs_value_nil();
                var txt = getComp<Text>(argv[0]);
                if (txt == null) return ZsNative.zs_value_nil();
                return zsStr(argv[1]) switch
                {
                    "text"     => ZsNative.zs_value_string(txt.text ?? ""),
                    "fontSize" => ZsNative.zs_value_int(txt.fontSize),
                    "color"    => writeColor(txt.color),
                    _          => ZsNative.zs_value_nil()
                };
            });

            ZsNativeFn textNewIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 3) return ZsNative.zs_value_nil();
                var txt = getComp<Text>(argv[0]);
                if (txt == null) return ZsNative.zs_value_nil();
                string key = zsStr(argv[1]);
                if      (key == "text")     txt.text     = zsStr(argv[2]);
                else if (key == "fontSize") txt.fontSize = (int)ZsNative.zs_value_as_int(argv[2]);
                else if (key == "color")    txt.color    = readColor(argv[2]);
                return ZsNative.zs_value_nil();
            });

            // ================================================================
            // Image
            // ================================================================
            ZsNativeFn imageIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 2) return ZsNative.zs_value_nil();
                var img = getComp<Image>(argv[0]);
                if (img == null) return ZsNative.zs_value_nil();
                return zsStr(argv[1]) switch
                {
                    "color"      => writeColor(img.color),
                    "fillAmount" => ZsNative.zs_value_float(img.fillAmount),
                    _            => ZsNative.zs_value_nil()
                };
            });

            ZsNativeFn imageNewIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 3) return ZsNative.zs_value_nil();
                var img = getComp<Image>(argv[0]);
                if (img == null) return ZsNative.zs_value_nil();
                string key = zsStr(argv[1]);
                if      (key == "color")      img.color      = readColor(argv[2]);
                else if (key == "fillAmount") img.fillAmount = (float)ZsNative.zs_value_as_float(argv[2]);
                return ZsNative.zs_value_nil();
            });

            // ================================================================
            // ScrollRect
            // ================================================================
            var scrollRectEvents = new Dictionary<int, (ZsUnityEvent<Vector2> ev, ZsValueHandle proxy)>();

            ZsNativeFn scrollRectIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 2) return ZsNative.zs_value_nil();
                var sr = getComp<ScrollRect>(argv[0]);
                if (sr == null) return ZsNative.zs_value_nil();
                return zsStr(argv[1]) switch
                {
                    "onValueChanged"               => getOrCreateEventProxy(
                                                        scrollRectEvents, sr.GetInstanceID(),
                                                        () => new ZsUnityEvent<Vector2>(rawVm, sr.onValueChanged,
                                                                v => ZsMarshal.Vector2(v)),
                                                        ev => ev.CreateProxy()),
                    "normalizedPosition"           => ZsMarshal.Vector2(sr.normalizedPosition),
                    "horizontalNormalizedPosition" => ZsNative.zs_value_float(sr.horizontalNormalizedPosition),
                    "verticalNormalizedPosition"   => ZsNative.zs_value_float(sr.verticalNormalizedPosition),
                    _                              => ZsNative.zs_value_nil()
                };
            });

            ZsNativeFn scrollRectNewIndexFn = pin((vm2, argc, argv) =>
            {
                if (argc < 3) return ZsNative.zs_value_nil();
                var sr = getComp<ScrollRect>(argv[0]);
                if (sr == null) return ZsNative.zs_value_nil();
                string key = zsStr(argv[1]);
                if (key == "normalizedPosition")
                    sr.normalizedPosition = new Vector2(fld(argv[2], "x"), fld(argv[2], "y"));
                else if (key == "horizontalNormalizedPosition")
                    sr.horizontalNormalizedPosition = (float)ZsNative.zs_value_as_float(argv[2]);
                else if (key == "verticalNormalizedPosition")
                    sr.verticalNormalizedPosition = (float)ZsNative.zs_value_as_float(argv[2]);
                return ZsNative.zs_value_nil();
            });

            // ================================================================
            // UGUI global table — factory functions
            // ================================================================
            IntPtr ugui = ZsNative.zs_table_new();

            ZsNative.zs_table_set_fn(ugui, "GetButton", pin((vm2, argc, argv) =>
                argc < 1 ? ZsNative.zs_value_nil()
                         : makeProxy<Button>(argv[0], buttonIndexFn, buttonNewIndexFn)), rawVm);

            ZsNative.zs_table_set_fn(ugui, "GetToggle", pin((vm2, argc, argv) =>
                argc < 1 ? ZsNative.zs_value_nil()
                         : makeProxy<Toggle>(argv[0], toggleIndexFn, toggleNewIndexFn)), rawVm);

            ZsNative.zs_table_set_fn(ugui, "GetSlider", pin((vm2, argc, argv) =>
                argc < 1 ? ZsNative.zs_value_nil()
                         : makeProxy<Slider>(argv[0], sliderIndexFn, sliderNewIndexFn)), rawVm);

            ZsNative.zs_table_set_fn(ugui, "GetInputField", pin((vm2, argc, argv) =>
                argc < 1 ? ZsNative.zs_value_nil()
                         : makeProxy<InputField>(argv[0], inputFieldIndexFn, inputFieldNewIndexFn)), rawVm);

            ZsNative.zs_table_set_fn(ugui, "GetText", pin((vm2, argc, argv) =>
                argc < 1 ? ZsNative.zs_value_nil()
                         : makeProxy<Text>(argv[0], textIndexFn, textNewIndexFn)), rawVm);

            ZsNative.zs_table_set_fn(ugui, "GetImage", pin((vm2, argc, argv) =>
                argc < 1 ? ZsNative.zs_value_nil()
                         : makeProxy<Image>(argv[0], imageIndexFn, imageNewIndexFn)), rawVm);

            ZsNative.zs_table_set_fn(ugui, "GetScrollRect", pin((vm2, argc, argv) =>
                argc < 1 ? ZsNative.zs_value_nil()
                         : makeProxy<ScrollRect>(argv[0], scrollRectIndexFn, scrollRectNewIndexFn)), rawVm);

            ZsNative.zs_vm_set_global(rawVm, "UGUI", ugui);
            ZsNative.zs_value_free(ugui);

            _pins = fns.ToArray();
        }
    }
}

#endif // ZSCRIPT_UGUI
