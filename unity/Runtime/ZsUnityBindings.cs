// ZsUnityBindings.cs — Registers essential Unity static APIs as ZScript globals.
//
// Called once from ZScriptVM.Awake() via ZsUnityBindings.Register(vm).
// Each Unity namespace becomes a ZScript global table:
//
//   Debug.Log / LogWarning / LogError / DrawLine / DrawRay
//   Mathf.Sin / Cos / Sqrt / Lerp / Clamp / PI / Infinity / …
//   Time.deltaTime / fixedDeltaTime / time / timeScale / frameCount / …
//   Application.Quit() / targetFrameRate / platform / isPlaying / …
//   Input.GetKey / GetAxis / mousePosition / anyKey / …
//   PlayerPrefs.SetInt / GetInt / SetFloat / GetFloat / SetString / GetString / …
//   SceneManager.LoadScene / GetActiveScene / sceneCount / …
//   Resources.Load / UnloadUnusedAssets
//   GameObject.Destroy / DontDestroyOnLoad / Find / FindWithTag / Instantiate
//
// Also registers bare globals:
//   IsValid(obj) → bool   (destroyed-object liveness check)
//   Destroy(obj, [delay]) (shorthand for GameObject.Destroy)
//
using System;
using System.Text;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace ZScript
{
    internal static class ZsUnityBindings
    {
        // Strong references — prevent GC from collecting delegates the C++ side holds.
        private static ZsNativeFn[] _pins;

        public static void Register(ZScriptVM vm)
        {
            IntPtr rawVm     = vm.RawVM;
            ZsObjectPool pool = vm.ObjectPool;
            var fns = new System.Collections.Generic.List<ZsNativeFn>();

            // ── local helpers ───────────────────────────────────────────────
            ZsNativeFn pin(ZsNativeFn fn) { fns.Add(fn); return fn; }

            // Read a UTF-8 string from a ZsValue.
            string zsStr(IntPtr v) {
                int len = ZsNative.zs_value_as_string(v, null, 0);
                byte[] buf = new byte[len + 1];
                ZsNative.zs_value_as_string(v, buf, buf.Length);
                return Encoding.UTF8.GetString(buf, 0, len);
            }

            // Read a float field from a ZScript table value.
            float fld(IntPtr tbl, string key) =>
                (float)ZsNative.zs_value_as_float(
                    ZsNative.zs_vm_handle_get_field(IntPtr.Zero, tbl, key));

            // Read a Vector3 from a ZScript table.
            Vector3 vec3(IntPtr v) => new Vector3(fld(v,"x"), fld(v,"y"), fld(v,"z"));

            // Read a Quaternion from a ZScript table.
            Quaternion quat(IntPtr v) => new Quaternion(fld(v,"x"), fld(v,"y"), fld(v,"z"), fld(v,"w"));

            // Wrap a Unity object in the pool → fresh ZsValue (caller owns).
            // Returns nil if obj is null or has been destroyed.
            IntPtr wrapObj(UnityEngine.Object obj) {
                if (obj == null) return ZsNative.zs_value_nil();
                long id = pool.Alloc(obj);
                return ZsNative.zs_vm_push_object_handle(rawVm, id);
            }

            // Set a ZScript global to a table, then free the ZsValueBox wrapper.
            // The inner ZTable lives on inside the VM via shared_ptr.
            void setGlobal(string name, IntPtr tbl) {
                ZsNative.zs_vm_set_global(rawVm, name, tbl);
                ZsNative.zs_value_free(tbl);
            }

            // ── Color helper ─────────────────────────────────────────────────
            Color readColor(IntPtr v) =>
                new Color(fld(v,"r"), fld(v,"g"), fld(v,"b"),
                    ZsNative.zs_value_type(v) == ZsType.Table ? fld(v,"a") : 1f);

            // ================================================================
            // IsValid(obj) — destroyed-object guard
            // ================================================================
            vm.RegisterFunction("IsValid", pin((vm2, argc, argv) => {
                if (argc < 1) return ZsNative.zs_value_bool(0);
                long id = ZsNative.zs_value_as_object(argv[0]);
                return ZsNative.zs_value_bool(id >= 0 && pool.IsValid(id) ? 1 : 0);
            }));

            // ================================================================
            // Debug
            // ================================================================
            {
                IntPtr t = ZsNative.zs_table_new();

                ZsNative.zs_table_set_fn(t, "Log", pin((vm2, argc, argv) => {
                    Debug.Log(argc > 0 ? zsStr(argv[0]) : "nil");
                    return ZsNative.zs_value_nil();
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "LogWarning", pin((vm2, argc, argv) => {
                    Debug.LogWarning(argc > 0 ? zsStr(argv[0]) : "nil");
                    return ZsNative.zs_value_nil();
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "LogError", pin((vm2, argc, argv) => {
                    Debug.LogError(argc > 0 ? zsStr(argv[0]) : "nil");
                    return ZsNative.zs_value_nil();
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "DrawLine", pin((vm2, argc, argv) => {
                    if (argc >= 2)
                        Debug.DrawLine(vec3(argv[0]), vec3(argv[1]),
                            argc >= 3 ? readColor(argv[2]) : Color.white);
                    return ZsNative.zs_value_nil();
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "DrawRay", pin((vm2, argc, argv) => {
                    if (argc >= 2)
                        Debug.DrawRay(vec3(argv[0]), vec3(argv[1]),
                            argc >= 3 ? readColor(argv[2]) : Color.white);
                    return ZsNative.zs_value_nil();
                }), rawVm);

                setGlobal("Debug", t);
            }

            // ================================================================
            // Mathf
            // ================================================================
            {
                IntPtr t = ZsNative.zs_table_new();

                void fn1(string n, Func<float,float> f) =>
                    ZsNative.zs_table_set_fn(t, n, pin((vm2, argc, argv) =>
                        ZsNative.zs_value_float(argc > 0
                            ? f((float)ZsNative.zs_value_as_float(argv[0])) : 0.0)), rawVm);

                void fn2(string n, Func<float,float,float> f) =>
                    ZsNative.zs_table_set_fn(t, n, pin((vm2, argc, argv) =>
                        ZsNative.zs_value_float(argc >= 2
                            ? f((float)ZsNative.zs_value_as_float(argv[0]),
                                (float)ZsNative.zs_value_as_float(argv[1])) : 0.0)), rawVm);

                void fn3(string n, Func<float,float,float,float> f) =>
                    ZsNative.zs_table_set_fn(t, n, pin((vm2, argc, argv) =>
                        ZsNative.zs_value_float(argc >= 3
                            ? f((float)ZsNative.zs_value_as_float(argv[0]),
                                (float)ZsNative.zs_value_as_float(argv[1]),
                                (float)ZsNative.zs_value_as_float(argv[2])) : 0.0)), rawVm);

                fn1("Sin",    Mathf.Sin);   fn1("Cos",   Mathf.Cos);
                fn1("Tan",    Mathf.Tan);   fn1("Asin",  Mathf.Asin);
                fn1("Acos",   Mathf.Acos);  fn1("Sqrt",  Mathf.Sqrt);
                fn1("Abs",    Mathf.Abs);   fn1("Sign",  Mathf.Sign);
                fn1("Floor",  Mathf.Floor); fn1("Ceil",  Mathf.Ceil);
                fn1("Round",  x => Mathf.Round(x));
                fn1("Log",    Mathf.Log);   fn1("Exp",   Mathf.Exp);

                fn2("Atan2",  Mathf.Atan2); fn2("Pow",   Mathf.Pow);
                fn2("Min",    Mathf.Min);   fn2("Max",   Mathf.Max);

                fn3("Lerp",       Mathf.Lerp);
                fn3("LerpAngle",  Mathf.LerpAngle);
                fn3("Clamp",      Mathf.Clamp);
                fn3("SmoothStep", Mathf.SmoothStep);
                fn3("MoveTowards",(a,b,d) => Mathf.MoveTowards(a,b,d));

                ZsNative.zs_table_set_fn(t, "Clamp01", pin((vm2, argc, argv) =>
                    ZsNative.zs_value_float(argc > 0
                        ? Mathf.Clamp01((float)ZsNative.zs_value_as_float(argv[0])) : 0.0)), rawVm);

                // Constants (set once; they are compile-time constants in C#)
                void setF(string n, double v) {
                    IntPtr fv = ZsNative.zs_value_float(v);
                    ZsNative.zs_table_set_value(t, n, fv);
                    ZsNative.zs_value_free(fv);
                }
                setF("PI",       Mathf.PI);
                setF("Infinity", float.PositiveInfinity);
                setF("Deg2Rad",  Mathf.Deg2Rad);
                setF("Rad2Deg",  Mathf.Rad2Deg);
                setF("Epsilon",  Mathf.Epsilon);

                setGlobal("Mathf", t);
            }

            // ================================================================
            // Time  (dynamic — __index reads fresh each access)
            // ================================================================
            {
                IntPtr t = ZsNative.zs_table_new();

                ZsNative.zs_table_set_fn(t, "__index", pin((vm2, argc, argv) => {
                    if (argc < 2) return ZsNative.zs_value_nil();
                    return zsStr(argv[1]) switch {
                        "deltaTime"            => ZsNative.zs_value_float(Time.deltaTime),
                        "fixedDeltaTime"       => ZsNative.zs_value_float(Time.fixedDeltaTime),
                        "time"                 => ZsNative.zs_value_float(Time.time),
                        "unscaledTime"         => ZsNative.zs_value_float(Time.unscaledTime),
                        "unscaledDeltaTime"    => ZsNative.zs_value_float(Time.unscaledDeltaTime),
                        "timeScale"            => ZsNative.zs_value_float(Time.timeScale),
                        "frameCount"           => ZsNative.zs_value_int(Time.frameCount),
                        "realtimeSinceStartup" => ZsNative.zs_value_float(Time.realtimeSinceStartup),
                        _ => ZsNative.zs_value_nil()
                    };
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "__newindex", pin((vm2, argc, argv) => {
                    if (argc >= 3) {
                        string key = zsStr(argv[1]);
                        if      (key == "timeScale")      Time.timeScale      = (float)ZsNative.zs_value_as_float(argv[2]);
                        else if (key == "fixedDeltaTime") Time.fixedDeltaTime = (float)ZsNative.zs_value_as_float(argv[2]);
                    }
                    return ZsNative.zs_value_nil();
                }), rawVm);

                setGlobal("Time", t);
            }

            // ================================================================
            // Application
            // ================================================================
            {
                IntPtr t = ZsNative.zs_table_new();

                ZsNative.zs_table_set_fn(t, "Quit", pin((vm2, argc, argv) => {
                    Application.Quit();
                    return ZsNative.zs_value_nil();
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "__index", pin((vm2, argc, argv) => {
                    if (argc < 2) return ZsNative.zs_value_nil();
                    return zsStr(argv[1]) switch {
                        "targetFrameRate"    => ZsNative.zs_value_int(Application.targetFrameRate),
                        "platform"           => ZsNative.zs_value_string(Application.platform.ToString()),
                        "isPlaying"          => ZsNative.zs_value_bool(Application.isPlaying ? 1 : 0),
                        "dataPath"           => ZsNative.zs_value_string(Application.dataPath),
                        "persistentDataPath" => ZsNative.zs_value_string(Application.persistentDataPath),
                        "version"            => ZsNative.zs_value_string(Application.version),
                        "productName"        => ZsNative.zs_value_string(Application.productName),
                        _ => ZsNative.zs_value_nil()
                    };
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "__newindex", pin((vm2, argc, argv) => {
                    if (argc >= 3 && zsStr(argv[1]) == "targetFrameRate")
                        Application.targetFrameRate = (int)ZsNative.zs_value_as_int(argv[2]);
                    return ZsNative.zs_value_nil();
                }), rawVm);

                setGlobal("Application", t);
            }

            // ================================================================
            // Input
            // ================================================================
            {
                IntPtr t = ZsNative.zs_table_new();

                KeyCode parseKey(string s) {
                    try { return (KeyCode)Enum.Parse(typeof(KeyCode), s, true); }
                    catch { return KeyCode.None; }
                }

                ZsNative.zs_table_set_fn(t, "GetKey", pin((vm2, argc, argv) =>
                    ZsNative.zs_value_bool(argc > 0 && Input.GetKey(parseKey(zsStr(argv[0]))) ? 1 : 0)), rawVm);
                ZsNative.zs_table_set_fn(t, "GetKeyDown", pin((vm2, argc, argv) =>
                    ZsNative.zs_value_bool(argc > 0 && Input.GetKeyDown(parseKey(zsStr(argv[0]))) ? 1 : 0)), rawVm);
                ZsNative.zs_table_set_fn(t, "GetKeyUp", pin((vm2, argc, argv) =>
                    ZsNative.zs_value_bool(argc > 0 && Input.GetKeyUp(parseKey(zsStr(argv[0]))) ? 1 : 0)), rawVm);
                ZsNative.zs_table_set_fn(t, "GetAxis", pin((vm2, argc, argv) =>
                    ZsNative.zs_value_float(argc > 0 ? Input.GetAxis(zsStr(argv[0])) : 0.0)), rawVm);
                ZsNative.zs_table_set_fn(t, "GetAxisRaw", pin((vm2, argc, argv) =>
                    ZsNative.zs_value_float(argc > 0 ? Input.GetAxisRaw(zsStr(argv[0])) : 0.0)), rawVm);
                ZsNative.zs_table_set_fn(t, "GetButton", pin((vm2, argc, argv) =>
                    ZsNative.zs_value_bool(argc > 0 && Input.GetButton(zsStr(argv[0])) ? 1 : 0)), rawVm);
                ZsNative.zs_table_set_fn(t, "GetButtonDown", pin((vm2, argc, argv) =>
                    ZsNative.zs_value_bool(argc > 0 && Input.GetButtonDown(zsStr(argv[0])) ? 1 : 0)), rawVm);
                ZsNative.zs_table_set_fn(t, "GetButtonUp", pin((vm2, argc, argv) =>
                    ZsNative.zs_value_bool(argc > 0 && Input.GetButtonUp(zsStr(argv[0])) ? 1 : 0)), rawVm);
                ZsNative.zs_table_set_fn(t, "GetMouseButton", pin((vm2, argc, argv) =>
                    ZsNative.zs_value_bool(argc > 0 && Input.GetMouseButton((int)ZsNative.zs_value_as_int(argv[0])) ? 1 : 0)), rawVm);
                ZsNative.zs_table_set_fn(t, "GetMouseButtonDown", pin((vm2, argc, argv) =>
                    ZsNative.zs_value_bool(argc > 0 && Input.GetMouseButtonDown((int)ZsNative.zs_value_as_int(argv[0])) ? 1 : 0)), rawVm);
                ZsNative.zs_table_set_fn(t, "GetMouseButtonUp", pin((vm2, argc, argv) =>
                    ZsNative.zs_value_bool(argc > 0 && Input.GetMouseButtonUp((int)ZsNative.zs_value_as_int(argv[0])) ? 1 : 0)), rawVm);

                ZsNative.zs_table_set_fn(t, "__index", pin((vm2, argc, argv) => {
                    if (argc < 2) return ZsNative.zs_value_nil();
                    return zsStr(argv[1]) switch {
                        "mousePosition"    => ZsMarshal.Vector3(Input.mousePosition),
                        "mouseScrollDelta" => ZsMarshal.Vector2(Input.mouseScrollDelta),
                        "anyKey"           => ZsNative.zs_value_bool(Input.anyKey    ? 1 : 0),
                        "anyKeyDown"       => ZsNative.zs_value_bool(Input.anyKeyDown? 1 : 0),
                        "touchCount"       => ZsNative.zs_value_int(Input.touchCount),
                        _ => ZsNative.zs_value_nil()
                    };
                }), rawVm);

                setGlobal("Input", t);
            }

            // ================================================================
            // PlayerPrefs
            // ================================================================
            {
                IntPtr t = ZsNative.zs_table_new();

                ZsNative.zs_table_set_fn(t, "SetInt", pin((vm2, argc, argv) => {
                    if (argc >= 2) PlayerPrefs.SetInt(zsStr(argv[0]), (int)ZsNative.zs_value_as_int(argv[1]));
                    return ZsNative.zs_value_nil();
                }), rawVm);
                ZsNative.zs_table_set_fn(t, "GetInt", pin((vm2, argc, argv) => {
                    if (argc < 1) return ZsNative.zs_value_int(0);
                    return ZsNative.zs_value_int(PlayerPrefs.GetInt(zsStr(argv[0]),
                        argc >= 2 ? (int)ZsNative.zs_value_as_int(argv[1]) : 0));
                }), rawVm);
                ZsNative.zs_table_set_fn(t, "SetFloat", pin((vm2, argc, argv) => {
                    if (argc >= 2) PlayerPrefs.SetFloat(zsStr(argv[0]), (float)ZsNative.zs_value_as_float(argv[1]));
                    return ZsNative.zs_value_nil();
                }), rawVm);
                ZsNative.zs_table_set_fn(t, "GetFloat", pin((vm2, argc, argv) => {
                    if (argc < 1) return ZsNative.zs_value_float(0.0);
                    return ZsNative.zs_value_float(PlayerPrefs.GetFloat(zsStr(argv[0]),
                        argc >= 2 ? (float)ZsNative.zs_value_as_float(argv[1]) : 0f));
                }), rawVm);
                ZsNative.zs_table_set_fn(t, "SetString", pin((vm2, argc, argv) => {
                    if (argc >= 2) PlayerPrefs.SetString(zsStr(argv[0]), zsStr(argv[1]));
                    return ZsNative.zs_value_nil();
                }), rawVm);
                ZsNative.zs_table_set_fn(t, "GetString", pin((vm2, argc, argv) => {
                    if (argc < 1) return ZsNative.zs_value_string("");
                    return ZsNative.zs_value_string(PlayerPrefs.GetString(zsStr(argv[0]),
                        argc >= 2 ? zsStr(argv[1]) : ""));
                }), rawVm);
                ZsNative.zs_table_set_fn(t, "HasKey", pin((vm2, argc, argv) =>
                    ZsNative.zs_value_bool(argc > 0 && PlayerPrefs.HasKey(zsStr(argv[0])) ? 1 : 0)), rawVm);
                ZsNative.zs_table_set_fn(t, "DeleteKey", pin((vm2, argc, argv) => {
                    if (argc > 0) PlayerPrefs.DeleteKey(zsStr(argv[0]));
                    return ZsNative.zs_value_nil();
                }), rawVm);
                ZsNative.zs_table_set_fn(t, "DeleteAll", pin((vm2, argc, argv) => {
                    PlayerPrefs.DeleteAll(); return ZsNative.zs_value_nil();
                }), rawVm);
                ZsNative.zs_table_set_fn(t, "Save", pin((vm2, argc, argv) => {
                    PlayerPrefs.Save(); return ZsNative.zs_value_nil();
                }), rawVm);

                setGlobal("PlayerPrefs", t);
            }

            // ================================================================
            // SceneManager
            // ================================================================
            {
                IntPtr t = ZsNative.zs_table_new();

                // ── sceneLoaded event proxy ──────────────────────────────────
                // ZsActionEvent subscribes once to C# event; distributes to all
                // registered ZScript closures.  The ZsActionEvent stays alive via
                // the C# event subscription holding its internal handler delegate.
                var sceneLoadedEvent = new ZsActionEvent<UnityEngine.SceneManagement.Scene, LoadSceneMode>(
                    rawVm,
                    h => SceneManager.sceneLoaded += h,
                    h => SceneManager.sceneLoaded -= h,
                    scene => ZsNative.zs_value_string(scene.name),
                    mode  => ZsNative.zs_value_int((int)mode));
                // Cache the proxy; __index returns a clone each access so all
                // ZScript references share the same underlying ZTable.
                ZsValueHandle sceneLoadedProxy = sceneLoadedEvent.CreateProxy();

                // ── sceneUnloaded event proxy ────────────────────────────────
                // sceneUnloaded is `event Action<Scene>` (one arg).  Build the
                // listener table manually and subscribe a persistent C# handler.
                var suListeners = new System.Collections.Generic.Dictionary<long, ZsValueHandle>();
                long suNextId = 1;
                ZsNativeFn suAddFn    = pin((vm2, argc, argv) => {
                    if (argc < 1) return ZsNative.zs_value_nil();
                    long id = suNextId++;
                    suListeners[id] = new ZsValueHandle(ZsNative.zs_value_clone(argv[0]));
                    return ZsNative.zs_value_int(id);
                });
                ZsNativeFn suRemFn    = pin((vm2, argc, argv) => {
                    if (argc >= 1) {
                        long id = ZsNative.zs_value_as_int(argv[0]);
                        if (suListeners.TryGetValue(id, out var h)) { h.Dispose(); suListeners.Remove(id); }
                    }
                    return ZsNative.zs_value_nil();
                });
                ZsNativeFn suRemAllFn = pin((vm2, argc, argv) => {
                    foreach (var h in suListeners.Values) h.Dispose();
                    suListeners.Clear();
                    return ZsNative.zs_value_nil();
                });
                IntPtr suTbl = ZsNative.zs_table_new();
                ZsNative.zs_table_set_fn(suTbl, "AddListener",        suAddFn,    rawVm);
                ZsNative.zs_table_set_fn(suTbl, "RemoveListener",     suRemFn,    rawVm);
                ZsNative.zs_table_set_fn(suTbl, "RemoveAllListeners", suRemAllFn, rawVm);
                ZsValueHandle sceneUnloadedProxy = new ZsValueHandle(suTbl);
                // Subscribe a single persistent handler that fans out to all listeners.
                SceneManager.sceneUnloaded += scene => {
                    IntPtr arg = ZsNative.zs_value_string(scene.name);
                    IntPtr[] suArgv = { arg };
                    byte[] suErr = new byte[256];
                    foreach (var fn in suListeners.Values) {
                        ZsNative.zs_value_invoke(rawVm, fn.Raw, 1, suArgv, out IntPtr r, suErr, suErr.Length);
                        if (r != IntPtr.Zero) ZsNative.zs_value_free(r);
                    }
                    ZsNative.zs_value_free(arg);
                };

                ZsNative.zs_table_set_fn(t, "LoadScene", pin((vm2, argc, argv) => {
                    if (argc < 1) return ZsNative.zs_value_nil();
                    if (ZsNative.zs_value_type(argv[0]) == ZsType.Int)
                        SceneManager.LoadScene((int)ZsNative.zs_value_as_int(argv[0]));
                    else
                        SceneManager.LoadScene(zsStr(argv[0]));
                    return ZsNative.zs_value_nil();
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "LoadSceneAdditive", pin((vm2, argc, argv) => {
                    if (argc < 1) return ZsNative.zs_value_nil();
                    if (ZsNative.zs_value_type(argv[0]) == ZsType.Int)
                        SceneManager.LoadScene((int)ZsNative.zs_value_as_int(argv[0]), LoadSceneMode.Additive);
                    else
                        SceneManager.LoadScene(zsStr(argv[0]), LoadSceneMode.Additive);
                    return ZsNative.zs_value_nil();
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "GetActiveScene", pin((vm2, argc, argv) => {
                    var scene = SceneManager.GetActiveScene();
                    IntPtr st = ZsNative.zs_table_new();
                    IntPtr sn = ZsNative.zs_value_string(scene.name);
                    IntPtr sp = ZsNative.zs_value_string(scene.path);
                    IntPtr si = ZsNative.zs_value_int(scene.buildIndex);
                    IntPtr sl = ZsNative.zs_value_bool(scene.isLoaded ? 1 : 0);
                    ZsNative.zs_table_set_value(st, "name",       sn);
                    ZsNative.zs_table_set_value(st, "path",       sp);
                    ZsNative.zs_table_set_value(st, "buildIndex", si);
                    ZsNative.zs_table_set_value(st, "isLoaded",   sl);
                    ZsNative.zs_value_free(sn); ZsNative.zs_value_free(sp);
                    ZsNative.zs_value_free(si); ZsNative.zs_value_free(sl);
                    return st; // caller (C++ shim) frees the box after extracting Value
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "__index", pin((vm2, argc, argv) => {
                    if (argc < 2) return ZsNative.zs_value_nil();
                    return zsStr(argv[1]) switch {
                        "sceneCount"                => ZsNative.zs_value_int(SceneManager.sceneCount),
                        "sceneCountInBuildSettings" => ZsNative.zs_value_int(SceneManager.sceneCountInBuildSettings),
                        // Return clones — all references share the same underlying ZTable.
                        "sceneLoaded"   => ZsNative.zs_value_clone(sceneLoadedProxy.Raw),
                        "sceneUnloaded" => ZsNative.zs_value_clone(sceneUnloadedProxy.Raw),
                        _ => ZsNative.zs_value_nil()
                    };
                }), rawVm);

                setGlobal("SceneManager", t);
            }

            // ================================================================
            // Resources
            // ================================================================
            {
                IntPtr t = ZsNative.zs_table_new();

                ZsNative.zs_table_set_fn(t, "Load", pin((vm2, argc, argv) => {
                    if (argc < 1) return ZsNative.zs_value_nil();
                    var obj = Resources.Load(zsStr(argv[0]));
                    return obj != null ? wrapObj(obj) : ZsNative.zs_value_nil();
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "UnloadUnusedAssets", pin((vm2, argc, argv) => {
                    Resources.UnloadUnusedAssets();
                    return ZsNative.zs_value_nil();
                }), rawVm);

                setGlobal("Resources", t);
            }

            // ================================================================
            // GameObject (static helpers; instance methods come from codegen)
            // ================================================================
            {
                // Shared Destroy logic — called both from GameObject.Destroy and bare Destroy.
                ZsNativeFn destroyImpl = pin((vm2, argc, argv) => {
                    if (argc < 1) return ZsNative.zs_value_nil();
                    long id = ZsNative.zs_value_as_object(argv[0]);
                    if (id < 0) return ZsNative.zs_value_nil();
                    if (pool.Get(id) is UnityEngine.Object uObj && uObj != null) {
                        float delay = argc >= 2 ? (float)ZsNative.zs_value_as_float(argv[1]) : 0f;
                        pool.Invalidate(id);
                        UnityEngine.Object.Destroy(uObj, delay);
                    }
                    return ZsNative.zs_value_nil();
                });

                IntPtr t = ZsNative.zs_table_new();

                ZsNative.zs_table_set_fn(t, "Destroy", destroyImpl, rawVm);

                ZsNative.zs_table_set_fn(t, "DontDestroyOnLoad", pin((vm2, argc, argv) => {
                    if (argc < 1) return ZsNative.zs_value_nil();
                    long id = ZsNative.zs_value_as_object(argv[0]);
                    if (id >= 0 && pool.Get(id) is UnityEngine.Object uObj)
                        UnityEngine.Object.DontDestroyOnLoad(uObj);
                    return ZsNative.zs_value_nil();
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "Find", pin((vm2, argc, argv) => {
                    if (argc < 1) return ZsNative.zs_value_nil();
                    var go = UnityEngine.GameObject.Find(zsStr(argv[0]));
                    return go != null ? wrapObj(go) : ZsNative.zs_value_nil();
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "FindWithTag", pin((vm2, argc, argv) => {
                    if (argc < 1) return ZsNative.zs_value_nil();
                    try {
                        var go = UnityEngine.GameObject.FindWithTag(zsStr(argv[0]));
                        return go != null ? wrapObj(go) : ZsNative.zs_value_nil();
                    } catch { return ZsNative.zs_value_nil(); }
                }), rawVm);

                ZsNative.zs_table_set_fn(t, "Instantiate", pin((vm2, argc, argv) => {
                    if (argc < 1) return ZsNative.zs_value_nil();
                    long id = ZsNative.zs_value_as_object(argv[0]);
                    if (id < 0 || !(pool.Get(id) is UnityEngine.Object prefab))
                        return ZsNative.zs_value_nil();
                    UnityEngine.Object inst = argc >= 3
                        ? UnityEngine.Object.Instantiate(prefab, vec3(argv[1]), quat(argv[2]))
                        : UnityEngine.Object.Instantiate(prefab);
                    return wrapObj(inst);
                }), rawVm);

                setGlobal("GameObject", t);

                // Bare global Destroy(obj, [delay]) — same implementation.
                vm.RegisterFunction("Destroy", destroyImpl);
            }

            _pins = fns.ToArray();
        }
    }
}
