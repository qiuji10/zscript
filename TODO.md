# ZScript — TODO

Tracks implementation tasks by phase. Status: `[ ]` todo, `[x]` done, `[-]` in progress.

---

## Phase 1 — Lexer + Parser + AST

### Lexer
- [x] Define token types (keywords, operators, literals, punctuation)
- [x] Hand-roll lexer: character stream → token stream
- [x] Handle string literals with `{expr}` interpolation markers
- [x] Handle `@unreal` / `@unity` annotation tokens
- [x] Line/column tracking for error reporting

### Parser
- [x] Recursive-descent parser skeleton
- [x] Parse variable declarations: `let` / `var` with optional type annotation
- [x] Parse function declarations: `fn name(params) -> ReturnType { }`
- [x] Parse class declarations with inheritance (`: Base`)
- [x] Parse trait declarations and `impl Trait for Type` blocks
- [x] Parse expressions: arithmetic, logical, comparison, string interpolation
- [x] Parse null safety operators: `?.`, `!.`, `?`-typed vars
- [x] Parse generics: `<T>` with lookahead disambiguation (avoid `<`/`>` ambiguity)
- [x] Parse delegates: `+=` / `-=` on event expressions
- [x] Parse engine-conditional blocks: `@unity { }` / `@unreal { }`
- [x] Parse class-level annotations: `@unreal.uclass`, `@unity.component`, etc.
- [x] Parse `for let i in range` and standard control flow (`if`, `while`, `return`)
- [x] Error recovery: collect all syntax errors, continue parsing

### AST
- [x] Define AST node types (one struct/class per construct)
- [x] AST pretty-printer (for debugging) — `zsc dump <file.zs>`
- [x] Source location attached to every node (for error messages + source maps)

---

## Phase 2 — Bytecode Compiler + VM (no GC, no hotpatch)

### Bytecode
- [x] Design instruction set (register-based, Lua 5 style)
- [x] Define binary `.zbc` format with magic header
- [x] Implement bytecode serializer / deserializer
- [x] Implement bytecode disassembler (`zsc disasm`)

### Compiler (AST → Bytecode)
- [x] Symbol table + scope resolution
- [x] Compile expressions to register operations
- [x] Compile variable declarations (`let` immutability enforced at compile time)
- [x] Compile function calls including generic calls
- [x] Compile class instantiation and method dispatch
- [x] Compile null safety: `?.` emits nil-check branch, `!.` emits force-unwrap + trap
- [x] Compile `@tag { }` blocks: strip at compile time based on active `TagSet`; any identifier valid as a tag (`@windows`, `@vulkan`, `@scenarioA`, etc.)
- [x] Compile string interpolation to concat ops
- [x] Compile delegate `+=` / `-=` to delegate object calls
- [x] Emit source maps (optional, for debugger)
- [x] Compile-time error: reassigning `let` binding
- [x] Compile-time error: missing explicit return type on `fn` (reported as a warning; existing scripts without annotations still run)
- [x] Compile-time error: mutable param without `mut` keyword (reassigning a non-`mut` param already triggers "cannot reassign immutable binding")

### Virtual Machine
- [x] Register-based interpreter loop
- [x] Call frame stack
- [x] Core type representation: `nil`, `bool`, `int`, `float`, `string`, `table`, `function`, `userdata`
- [x] String interning
- [x] Table (hash map) implementation
- [x] Function objects + closures (upvalue capture)
- [x] `vm.add_tag(name)` / `vm.remove_tag(name)` / `vm.has_tag(name)` — generic tag set replaces engine mode; validated identifiers only
- [x] `vm.load_file(path)` — load and execute a `.zs` script
- [x] `vm.call(name, args...)` — call a named script function from C++
- [x] Re-entrant `invoke_from_native` for HOF calls from native C++ functions

### Language Features
- [x] `match` statement with pattern arms
- [x] Power operator (`**`) and compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`, `**=`)
- [x] `for k, v in table` KV iteration
- [x] `try` / `catch` / `throw` error handling
- [x] Variadic functions (`...args` collected as array)
- [x] Multiple return values (`return a, b` + `let x, y = fn()`)
- [x] `is` type-check operator — walks `__class__`/`__base__` chain + trait markers
- [x] Array destructuring: `let [a, b, ...rest] = arr`
- [x] Table destructuring: `let {x, y: alias} = obj` (top-level and inside functions)
- [x] Static methods and fields (`static fn foo()`, `static var count = 0`) — stored on class prototype, skipped during instantiation
- [x] Default parameter values (`fn f(x = 1, y = 2)`)
- [x] Traits: `trait T { fn method() }` and `impl T for Class {}` with default method inheritance
- [x] Array built-in methods: `push`, `pop`, `slice`, `index_of`, `reverse`, `concat`, `first`, `last`, `map`, `filter`, `reduce`, `each`, `any`, `all`, `flat`
- [x] Table built-in methods: `keys`, `values`, `contains`, `remove`, `len`
- [x] Named arguments `f(x: 1, y: 2)` — runtime-resolved via `param_names` in Proto; works with free functions, methods, closures; mixes with positional args; triggers default values for skipped params
- [x] Optional chaining improvements (`?.` / `!.` on method chains) — multi-level chains, `!.method()` self dispatch, `?.[i]` / `!.[i]` safe/force subscript
- [x] Delegate `+=` / `-=` event subscription / unsubscription — `ZDelegate` type, `DelegateAdd`/`DelegateSub` opcodes; `obj.ev()` fires all handlers; arithmetic `+=`/`-=` still works on non-callable RHS
- [x] String built-in methods: already implemented — see stdlib `string` table
- [x] String multiline / raw literals — backtick strings `` `hello\nworld` `` (no escape processing, no interpolation, multiline)
- [x] Range type and range iteration (`0..10`, `0..<10`) — first-class range values via `NewRange`/`NewRangeExcl` opcodes; `TLen` and `GetIndex` handle them transparently; passable to functions
- [x] Generics / type parameters — type params as leading locals; `T()`, `item is T`, `T == "int"` patterns; `IsInstanceDynamic` opcode for runtime `is T` checks

---

## Phase 3 — GC + Module System + C++ Binding API

### Garbage Collector
- [x] Incremental mark-and-sweep GC (tri-color)
- [x] Write barrier for incremental correctness
- [x] `pin` / `unpin` API for C++ side to hold objects
- [x] GC tuning knobs (step_alloc_limit)
- [ ] (Stretch) Generational GC upgrade path

### Module System
- [x] Each `.zs` file = one independently loadable module
- [x] Module registry (name → compiled module, cached)
- [x] Module load / execute lifecycle
- [x] Circular import detection (via Loading state)
- [x] Native module registration from C++
- [x] Custom source provider (for embedding without FS)

### C++ Binding API
- [x] Arg extraction helpers (`ArgOf<T>`) and return helpers (`RetOf<T>`)
- [x] `vm.register_function(name, lambda)` — free function registration
- [x] `register_class<T>(vm, name)` fluent builder:
  - [x] `.constructor<Args...>()`
  - [x] `.method(name, &T::method)`
  - [x] `.property(name, &T::field)`
- [x] Object registry (raw ptr → shared_ptr) for self extraction
- [x] Error propagation: C++ exceptions → ZScript runtime errors

### Macro Helper Layer
- [x] `ZSCRIPT_CLASS` / `ZSCRIPT_METHOD` / `ZSCRIPT_PROPERTY` / `ZSCRIPT_END` macros
- [x] Self-registering static initializer pattern (`AutoRegister`)
- [x] `ZScript::register_all(vm)` — bulk registration entry point

---

## Phase 4 — Hotpatch System + FileWatcher

### File Watcher
- [x] Platform abstraction interface (`IWatcherBackend`)
- [x] Windows: `ReadDirectoryChangesW` backend
- [x] Linux: `inotify` backend
- [x] macOS: `FSEvents` backend (GCD dispatch queue)
- [x] iOS / Android: polling fallback (configurable interval)
- [x] 50ms debounce after last change before firing recompile

### Hotpatch Manager
- [x] Module version counter
- [x] Background recompile on file change
- [x] Atomic module pointer swap (new bytecode in, old bytecode kept alive)
- [x] Global variable migration by name (old → new module version)
- [x] `on_reload(old: Any) -> Any` callback per module (user-defined)
- [x] `vm.enable_hotpatch(dir)` — watch a directory
- [x] `vm.poll()` — apply queued hotpatches at safe point (between frames)
- [x] Thread safety: FileWatcher thread enqueues, VM main thread drains at `poll()`

---

## Phase 5 — Engine Plugins

### Unreal Engine Plugin (`ZScriptPlugin`)
- [ ] UE Plugin scaffold (`ZScriptPlugin.uplugin`)
- [ ] `UZScriptSubsystem` (Game Instance Subsystem) owns the VM
- [ ] Bind `UObject` subclasses via existing `UCLASS / UPROPERTY / UFUNCTION` metadata
- [ ] Blueprint node: `Call ZScript Function`
- [ ] Editor integration: hotpatch on script save in editor
- [ ] `@unreal.uclass` / `@unreal.uproperty` annotation handling in compiler

### Unity Plugin

#### VM-level prerequisites
- [ ] `__index` metamethod support in VM — when a table lookup misses, call `table.__index(key)` if present; enables property-style access on proxy objects (`obj.position`, `obj.localScale`)
- [ ] `__newindex` metamethod support in VM — when assigning to a missing key, call `table.__newindex(key, value)`; enables write-back to Unity objects (`obj.position = Vec3(0, 1, 0)`)
- [ ] `__call` metamethod support in VM — lets a proxy table be called as a function; needed for struct constructors (`Vector3(1, 0, 0)`)
- [ ] `vm.push_object_handle(id)` / `vm.get_object_handle(val)` — opaque integer handle system for passing Unity object references across the C boundary without GC interaction

#### C API (`src/unity/zscript_c.h` + `src/unity/zscript_c.cpp`)
- [ ] Opaque handle types: `typedef void* ZsVM; typedef void* ZsValue;` — safe across P/Invoke
- [ ] VM lifecycle: `zs_vm_new()`, `zs_vm_free(vm)`, `zs_vm_open_stdlib(vm)`
- [ ] Script execution: `zs_vm_load_file(vm, path, err_buf, err_len)`, `zs_vm_load_source(vm, name, src, err_buf, err_len)`
- [ ] Function calls: `zs_vm_call(vm, name, argc, argv, out_result)` — takes array of `ZsValue`, returns single `ZsValue`
- [ ] Value constructors: `zs_value_nil()`, `zs_value_bool(b)`, `zs_value_int(n)`, `zs_value_float(f)`, `zs_value_string(s)`, `zs_value_object(handle_id)`
- [ ] Value accessors: `zs_value_type(v)`, `zs_value_as_bool(v)`, `zs_value_as_int(v)`, `zs_value_as_float(v)`, `zs_value_as_string(v)`, `zs_value_as_object(v)`
- [ ] Value lifetime: `zs_value_free(v)`, `zs_value_clone(v)` — C# side manages `ZsValueHandle : SafeHandle`
- [ ] Native function registration: `zs_vm_register_fn(vm, name, fn_ptr)` — `fn_ptr` is `ZsNativeFn = ZsValue(*)(ZsVM, int argc, ZsValue* argv)`
- [ ] Hotpatch: `zs_vm_enable_hotpatch(vm, dir)`, `zs_vm_poll(vm)` — callable from `Update()`
- [ ] Tag system: `zs_vm_add_tag(vm, tag)`, `zs_vm_remove_tag(vm, tag)`, `zs_vm_has_tag(vm, tag)`
- [ ] Error query: `zs_vm_last_error(vm, buf, len)` — retrieve last error message after a failed call

#### Native plugin build
- [ ] CMake shared library target `zscript_unity` — links core ZScript, exports only `zs_*` symbols
- [ ] Windows: outputs `zscript_unity.dll` + import lib; CI copies to `unity/Plugins/x86_64/`
- [ ] macOS: outputs `zscript_unity.bundle` (Unity expects `.bundle` for editor); CI copies to `unity/Plugins/`
- [ ] Linux: outputs `libzscript_unity.so`; CI copies to `unity/Plugins/x86_64/`
- [ ] Android (arm64-v8a): cross-compile with NDK toolchain; CI copies to `unity/Plugins/Android/libs/arm64-v8a/`
- [ ] iOS: static `.a` (dynamic loading disallowed on iOS App Store); CI copies to `unity/Plugins/iOS/`
- [ ] `unity/Plugins/**/*.meta` files — Unity plugin importer settings (platform, architecture, CPU) committed alongside native libs

#### C# runtime layer (`unity/Runtime/`)
- [ ] `ZsValueHandle.cs` — `SafeHandle` subclass; `ReleaseHandle()` calls `zs_value_free()`; prevents leaks when C# GC collects the wrapper
- [ ] `ZsVM.cs` — `[DllImport]` declarations for all `zs_*` C API functions; `const string Lib = "zscript_unity"`
- [ ] `ZScriptVM.cs` — `MonoBehaviour`; owns a `ZsVM` handle; calls `zs_vm_poll()` in `Update()`; exposes `LoadFile()`, `Call()`, `AddTag()` to other components; `OnDestroy()` calls `zs_vm_free()`
- [ ] `ZScriptModule.cs` — `ScriptableObject` asset that holds a `.zs` file reference; drag-and-drop in Inspector; `ZScriptVM` loads it on `Start()`
- [ ] Object pool (`ZsObjectPool.cs`) — `List<object>` + `Dictionary<long, object>` (handle→object, object→handle); assigns monotonically increasing integer IDs; `Alloc(obj)`, `Get(id)`, `Release(id)`; no `GCHandle` pinning needed since C side only sees the integer

#### Built-in Unity type bindings — edit-time codegen tool (`tools/UnityBindingGen/`)
- [ ] C# console tool (`UnityBindingGen.exe`) using Roslyn (`Microsoft.CodeAnalysis`) — invoked from Unity Editor menu or CI; zero runtime reflection
- [ ] Scans `UnityEngine.dll` (and `UnityEngine.CoreModule.dll`, etc.) in the current Unity install; discovers public types, methods, properties, fields
- [ ] Generates one `.cpp` file per Unity type (e.g., `unity_bindings/Transform_binding.cpp`) with a `register_Transform(VM& vm)` function that:
  - Creates a ZScript class table named `"Transform"` (exact C# name, no snake_case conversion)
  - For each public method `Foo(args)`: registers a native fn `"Foo"` that extracts `object_handle`, calls `zs_object_call_method(handle, "Foo", ...)` via C API dispatch
  - For each readable property `Bar`: registers `__index` handler case `"Bar"` → `zs_object_get_prop(handle, "Bar")`
  - For each writable property `Bar`: registers `__newindex` handler case `"Bar"` → `zs_object_set_prop(handle, "Bar", value)`
  - All names are the literal C# identifier — no transformation applied
- [ ] Generates a C# dispatch file (`ZsUnityDispatch.cs`) with a `static void Dispatch(long handle, string method, ...)` method using `switch` on `method` name; delegates to `static readonly Action<...>` fields (IL2CPP safe — no `MethodInfo.Invoke`)
- [ ] Priority type list for initial codegen: `Transform`, `GameObject`, `Rigidbody`, `Rigidbody2D`, `Collider`, `Collider2D`, `Camera`, `Light`, `AudioSource`, `Animator`, `NavMeshAgent`, `ParticleSystem`, `Canvas`, `RectTransform`, `Image`, `Text`, `Button`
- [ ] Struct types (`Vector2`, `Vector3`, `Vector4`, `Quaternion`, `Color`, `Rect`, `Bounds`) — marshalled by value as ZScript tables with `x`/`y`/`z`/`w` fields; `__call` metamethod on the class table acts as constructor
- [ ] `register_unity_builtins(VM& vm)` — bulk entry point called by `ZScriptVM` at startup; calls all per-type `register_*` fns

#### `[ZScriptExport]` codegen for user C# classes (`tools/ZScriptExportGen/`)
- [ ] MSBuild / Unity Editor source generator: scans all assemblies for types tagged `[ZScriptExport]`
- [ ] For each exported type: generates a `register_<TypeName>(VM& vm)` C++ snippet and a C# dispatch stub — same name-preserving approach as built-in codegen
- [ ] Supports: methods, get/set properties, public fields — all three surface as plain `obj.Name` / `obj.Name = v` in ZScript via `__index`/`__newindex`
- [ ] Pure C# classes (non-MonoBehaviour): `__call` metamethod registered as constructor — `MyClass()` in ZScript calls `new MyClass()` in C#; `ZsObjectPool` holds a strong reference so C# GC does not collect the object while ZScript holds the handle
- [ ] MonoBehaviour classes: `__call` disabled (engine manages instantiation via `AddComponent`); ZScript receives the handle from C# side after `AddComponent` completes
- [ ] Handle release: when ZScript GC drops the last reference to a handle, `zs_value_free()` calls `pool.Release(id)` — the strong C# reference is dropped and GC can collect the object
- [ ] Static classes (e.g. `Physics`, `Time`, `Input`, `Screen`, `Mathf`): registered as a global ZScript table (no object handle, no `__call`); static methods → table functions; static fields/properties → `__index`/`__newindex` on the table; `Time.deltaTime`, `Input.mousePosition`, `Physics.gravity` etc. work identically to C#
- [ ] Generic methods (e.g. `GameObject.FindObjectsWithType<T>()`, `GetComponent<T>()`): ZScript's own generic syntax works naturally — `FindObjectsWithType<Camera>()` passes the `Camera` class table as `T`; native binding reads `T.__name` to get the type name string and dispatches into a pre-generated C# `static readonly Dictionary<string, Func<...>>`; no `MakeGenericMethod` at runtime (IL2CPP safe); ZScript generic functions can forward `T` transparently; binding exposes only Unity's actual API — no invented convenience wrappers; unknown type names return empty array or raise ZScript error; codegen emits a build-time warning listing covered types
- [ ] `[ZScriptExport(Name = "MyAlias")]` optional name override for the ZScript-facing identifier
- [ ] Methods marked `[ZScriptHide]` are excluded from codegen output

#### Coroutine / async support (parity with Unity Lua)
- [ ] ZScript coroutine type — `coroutine.create(fn)`, `coroutine.resume(co)`, `coroutine.yield(value)` mirroring Lua coroutine API; VM adds a coroutine frame stack alongside the call frame stack
- [ ] Unity coroutine bridge — `StartCoroutine(fn)` in ZScript starts a Unity `IEnumerator` coroutine; the ZScript coroutine is resumed each frame until it returns; `yield WaitForSeconds(n)`, `yield WaitForEndOfFrame()`, `yield WaitForFixedUpdate()` yield values recognised by the bridge and forwarded to Unity's scheduler
- [ ] `yield WaitUntil(fn)` and `yield WaitWhile(fn)` — predicate-based yield; ZScript closure passed as predicate, evaluated each frame by Unity
- [ ] `StopCoroutine(handle)` — cancel a running ZScript coroutine by handle

#### C# event → ZScript delegate bridging
- [ ] `UnityEvent` binding — `button.onClick` returns a `UnityEvent` proxy handle; the proxy exposes `AddListener(fn)`, `RemoveListener(fn)`, `RemoveAllListeners()`, `Invoke()` as methods; C# side wraps the ZScript closure in a `UnityAction` on `AddListener`, stores it keyed by closure identity for later `RemoveListener` lookup
- [ ] Typed `UnityEvent<T>` binding — `slider.onValueChanged` returns a `UnityEvent<float>` proxy; `AddListener(fn(value) { ... })` wraps the closure in a `UnityAction<float>`; argument marshalled through C API when the event fires
- [ ] `UnityEvent<T0,T1>` variants — same pattern for two-argument events
- [ ] C# `event` (keyword) subscription — for C# `event Action` / `event Action<T>` fields on exported types, ZScript `+=`/`-=` delegate syntax applies (distinct from `UnityEvent` which uses `AddListener`); generated adapter wraps ZScript closure in a managed `static readonly` delegate field (IL2CPP safe)
- [ ] `SceneManager.sceneLoaded` / `SceneManager.sceneUnloaded` — C# `event Action<Scene, LoadSceneMode>`; subscribable via ZScript `+=`/`-=`
- [ ] Lifecycle event callbacks for `@unity.component` classes — Unity calls `Start()`, `Update()`, `FixedUpdate()`, `LateUpdate()`, `OnDestroy()`, `OnEnable()`, `OnDisable()` on the generated MonoBehaviour bridge which forwards each call into the ZScript component's matching function if defined; function names are exact Unity names

#### Unity physics + collision callbacks
- [ ] `OnCollisionEnter(collision)`, `OnCollisionStay(collision)`, `OnCollisionExit(collision)` — forwarded into ZScript component function of same name; `collision` passed as a bound `Collision` object handle
- [ ] `OnTriggerEnter(other)`, `OnTriggerStay(other)`, `OnTriggerExit(other)` — same pattern; `other` is a `Collider` handle
- [ ] `OnCollisionEnter2D` / `OnTriggerEnter2D` variants — 2D physics equivalents

#### Destroyed object safety
- [ ] `IsValid(obj)` built-in in ZScript Unity layer — checks `obj != null` on C# side using Unity's overloaded `==`; returns `false` for destroyed `UnityEngine.Object` instances even if the handle integer is still live
- [ ] Auto-validity check on every dispatch — before calling any method or property through the C API, check `UnityEngine.Object` liveness; raise a ZScript error with the object type name instead of crashing
- [ ] Handle invalidation on `Destroy(obj)` — `Destroy(obj)` binding marks the pool entry as invalid immediately so subsequent ZScript accesses fail fast with a clear error

#### Essential Unity API bindings (missing from current priority list)
- [ ] `GameObject`: `Instantiate(prefab)`, `Instantiate(prefab, position, rotation)`, `Destroy(obj)`, `Destroy(obj, delay)`, `DontDestroyOnLoad(obj)`, `SetActive(bool)`, `CompareTag(tag)`, `AddComponent<T>()`, `GetComponent<T>()`, `GetComponents<T>()`, `GetComponentInChildren<T>()`, `GetComponentInParent<T>()`
- [ ] `SceneManager`: `LoadScene(name)`, `LoadScene(index)`, `LoadSceneAsync(name)`, `GetActiveScene()`, `sceneLoaded` event
- [ ] `Resources`: `Load<T>(path)`, `LoadAll<T>(path)`, `UnloadUnusedAssets()`
- [ ] `PlayerPrefs`: `SetInt`, `SetFloat`, `SetString`, `GetInt`, `GetFloat`, `GetString`, `HasKey`, `DeleteKey`, `Save`
- [ ] `Debug`: `Log(msg)`, `LogWarning(msg)`, `LogError(msg)`, `DrawLine(start, end, color)`, `DrawRay(origin, dir, color)` — routes through Unity console with proper context (object ping, frame number)
- [ ] `Application`: `Quit()`, `targetFrameRate`, `platform`, `isPlaying`, `dataPath`, `persistentDataPath`
- [ ] `Input` (Legacy): `GetKey`, `GetKeyDown`, `GetKeyUp`, `GetAxis`, `GetButton`, `GetButtonDown`, `mousePosition`, `GetMouseButton`, `GetMouseButtonDown`
- [ ] `Mathf`: full binding — `Sin`, `Cos`, `Sqrt`, `Lerp`, `Clamp`, `Clamp01`, `Abs`, `Round`, `Floor`, `Ceil`, `Pow`, `Atan2`, `PI`, `Infinity`, `Deg2Rad`, `Rad2Deg`
- [ ] UGUI widgets: `Button` (`onClick.AddListener`, `onClick.RemoveListener`), `Toggle` (`onValueChanged.AddListener`, `isOn`), `Slider` (`onValueChanged.AddListener`, `value`, `minValue`, `maxValue`), `InputField` (`onValueChanged.AddListener`, `onEndEdit.AddListener`, `text`), `Text` (`text`, `fontSize`, `color`), `Image` (`sprite`, `color`, `fillAmount`), `ScrollRect` (`onValueChanged.AddListener`, `normalizedPosition`)

#### Annotation handling in compiler + VM (`@unity.*`)
- [ ] `@unity.component` on a ZScript class — VM registers it as a component type; `ZScriptVM` can instantiate and attach it to a `GameObject` via `AddComponent`-equivalent C API call
- [ ] `@unity.serialize` on a field — field is exposed in the Unity Inspector via a generated `[SerializeField]` wrapper in the MonoBehaviour bridge
- [ ] `@unity.adapter` on a ZScript class — declares that the class wraps a Unity built-in type; VM uses `__index`/`__newindex` dispatch to route property access through the C API

#### Editor extension (`unity/Editor/`)
- [ ] `ZScriptEditorWatcher.cs` — `[InitializeOnLoad]` class; uses `FileSystemWatcher` pointing at the project's `ZScripts/` folder; on change: calls `ZScriptVM.Instance.Reload(path)` if in Play mode
- [ ] `ZScriptEditorWindow.cs` — Unity Editor window (`Window > ZScript`) showing: loaded modules list, reload count, last error, manual reload button
- [ ] `ZScriptImporter.cs` — `ScriptedImporter` for `.zs` files so Unity tracks them as assets; double-click opens VS Code with LSP; saves `zsc check` errors to Console
- [ ] Auto-reload on entering Play mode — `EditorApplication.playModeStateChanged` hook re-runs all loaded modules so script state resets cleanly

#### IL2CPP compatibility audit
- [ ] No `Type.GetMethods()` / `Type.GetProperties()` / `MethodInfo.Invoke()` at runtime — all dispatch through generated `switch` statements and `static readonly` delegate fields
- [ ] No `Activator.CreateInstance()` — constructors invoked through explicit generated factory delegates
- [ ] No `Assembly.GetTypes()` at runtime — type scanning happens only in Editor codegen tool (Mono, not IL2CPP)
- [ ] All `[DllImport]` in `ZsVM.cs` use `CallingConvention.Cdecl` explicitly — required for IL2CPP P/Invoke
- [ ] `link.xml` rule: `<assembly fullname="ZScriptRuntime" preserve="all"/>` — prevents IL2CPP stripping of binding glue
- [ ] `[Preserve]` attribute on all dispatch methods and generated adapter classes
- [ ] AOT-compile test: build a minimal IL2CPP player and verify no `ExecutionEngineException` at startup

---

## Phase 6 — Tooling

### CLI (`zsc`)
- [x] `zsc compile <file.zs> -o <file.zbc>` — compile to bytecode
- [x] `zsc run <file.zs>` — compile + execute
- [x] `zsc check <file.zs>` — compile only, report errors
- [x] `zsc disasm <file.zbc>` — disassemble bytecode
- [x] `--tag=<name>` flag (repeatable; activates a `@tag { }` block; replaces `--engine=`)
- [x] Human-readable error output with line/column
- [x] Shortcut: `zsc <file.zs>` acts as `zsc run <file.zs>`

### LSP Server
- [x] LSP server — JSON-RPC stdio transport, full message loop
- [x] `textDocument/completion` — prefix-filtered identifiers, dot-completion for class members and stdlib modules (math, string, table, io), keywords, stdlib builtins
- [x] `textDocument/definition` — go-to-definition for functions, classes, variables, members
- [x] `textDocument/hover` — signature + type info on hover
- [x] `textDocument/signatureHelp` — active parameter highlighting on `(` / `,`
- [x] `textDocument/publishDiagnostics` — inline lex/parse/compile errors + undefined symbol warnings
- [x] VS Code extension — TypeScript client, syntax highlighting (TextMate grammar), DAP wiring, server-path auto-detect, restart command

### Debugger (DAP)
- [x] DAP server — `src/dap.cpp` / `src/dap.h`, stdio transport, full message loop
- [x] Breakpoint set/clear — verified, line-level
- [x] Step over / step into / step out
- [x] Variable inspection (locals, globals) — local_names captured per frame
- [x] Stack frame display — multi-frame with source/line info
- [x] Script output routed as DAP `output` events (stdout not corrupted)

---

## Ongoing / Cross-cutting

- [-] Standard library
  - [x] `print`, `to_string`, `to_int`, `to_float`, `type_of`, `len` / `#` operator
  - [x] `math`: `floor`, `ceil`, `sqrt`, `abs`, `min`, `max`, `pow`, `round`
  - [x] Array methods (see Language Features above)
  - [x] Table methods (see Language Features above)
  - [x] `string`: `len`, `sub`, `upper`, `lower`, `trim`, `trim_start`, `trim_end`, `contains`, `starts_with`, `ends_with`, `find`, `replace`, `split`, `join`, `rep`, `byte`, `char`, `format`, `is_empty`, `reverse`
  - [x] `json`: `encode(value) -> string`, `decode(string) -> value` (handles nil, bool, int, float, string, arrays, objects; round-trips)
  - [x] `io`: `read_file`, `write_file`, `append_file`, `lines`, `size`, `exists`, `delete_file`, `rename`, `copy_file`, `read_line`, `print_err`
  - [x] `os`: `getcwd`, `chdir`, `mkdir`, `rmdir`, `listdir`, `getenv`, `time`, `clock`, `platform`, `exit`; `os.path`: `join`, `dirname`, `basename`, `stem`, `ext`, `abs`, `exists`, `is_file`, `is_dir`
- [x] Sandbox approach: `open_io()` / `open_os()` are separate opt-in calls — not included by `open_stdlib()`; host controls file/OS access
- [x] Test suite: one test file per language feature (lexer, parser, vm, operators, control_flow, functions, classes, collections, closures, error_handling, stdlib, gc, hotpatch)
- [x] Fuzzer for lexer + parser — libFuzzer harness in `fuzz/`, 20-seed corpus, CI 30 s smoke run
- [x] CI: build matrix (Windows / Linux / macOS)
- [x] Decide: GC algorithm — mark-sweep (implemented)
- [x] Decide: hotpatch granularity — file-level (implemented)
- [x] Decide: bytecode portability — binary + magic header (implemented)
