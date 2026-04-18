# Unity Integration

This page is the public integration guide for the Unity runtime package.

The Unity package in this repository is:

- `unity/com.zscript.runtime`

The development project used for validation is:

- `unity/UnityProject`

## What The Unity Package Provides

- a native plugin bridge into the ZScript VM
- a C# runtime wrapper around the C API
- Editor integration for `.zs` assets
- EditMode tests for the package
- generated and hand-written Unity-facing runtime helpers
- Inspector serialization for ZScript class fields
- adapter class registry for wrapping C# objects in ZScript APIs
- export binding codegen for exposing C# types to ZScript
- IL2CPP link.xml generation for AOT builds

## Package Layout

Important package areas include:

- `Runtime/` — MonoBehaviours, value handles, object pool, event bridges, serialization, adapters
- `Editor/` — importers, Editor windows, custom inspectors, codegen tools
- `Tests/EditMode/` — automated tests

## Plugin Binaries

The `Plugins/` directories in git are mostly placeholders plus `.meta` files.

Native plugin binaries are build outputs and are installed during local builds or CI packaging. They are not treated as normal source files in the repository.

## Development Project Setup

The Unity development project references the package through `Packages/manifest.json` with a local file dependency.

That allows the Unity test project to validate the package directly from the repository checkout.

## Building The Native Unity Plugin

```bash
cmake -B build-unity -DZSCRIPT_UNITY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-unity --config Release --target zscript_unity --parallel
cmake --install build-unity --config Release
```

By default, installation targets the package's `Plugins/` tree using the platform-specific layout configured in `CMakeLists.txt`.

## Running Unity EditMode Tests

The repository CI runs Unity EditMode tests against `unity/UnityProject`.

The package tests live in:

- `unity/com.zscript.runtime/Tests/EditMode`

EditMode tests validate:

- VM lifecycle in Unity
- annotation and tag behavior through the Unity wrapper
- serialized field injection (`@unity.serialize`)
- bridge-layer object and event behavior
- Unity-specific regressions that native tests alone cannot catch

---

## Annotations In Unity

Unity is one of the main reasons ZScript has first-class annotation metadata.

### @unity.component

Marks a ZScript class as a Unity component. `ZScriptVM.GetComponentClasses()` returns all classes carrying this annotation, which can be used to build component pickers in the Editor.

```ts
@unity.component
class PlayerController {
    fn start() { }
    fn update() { }
}
```

Attach it to a GameObject at runtime:

```csharp
vm.AttachComponent(gameObject, "PlayerController");
```

### @unity.serialize

Marks a ZScript class as having inspector-editable fields. Attach a `ZsSerializedFields` component alongside `ZsComponentBridge` to expose those fields in the Unity Inspector.

```ts
@unity.serialize
@unity.component
class Enemy {
    var hp    = 100
    var speed = 3.5
    var name  = "goblin"

    fn start() {
        // hp, speed, name are already set from the Inspector before this runs
    }
}
```

In the Inspector, add a `ZsSerializedFields` component to the same GameObject. Add field entries whose names match the ZScript field names exactly. `ZsComponentBridge` injects those values before `start()` is called.

Supported field types: `Bool`, `Int`, `Float`, `String`.

You can also add the component programmatically:

```csharp
var sf = gameObject.AddComponent<ZsSerializedFields>();
sf.SetField("hp",    ZsFieldType.Int,   iVal: 200);
sf.SetField("speed", ZsFieldType.Float, fVal: 4.0f);
sf.SetField("name",  ZsFieldType.String, sVal: "orc");
```

### @unity.adapter

Marks a ZScript class as a type adapter — a script-side wrapper for a C# object that provides a clean ZScript API.

```ts
@unity.adapter
class CameraAdapter {
    fn new(proxy) { self._cam = proxy }

    fn fieldOfView()      { return self._cam.fieldOfView }
    fn setFieldOfView(fov) { self._cam.fieldOfView = fov }
}
```

After scripts are loaded, the adapter registry is populated automatically. Use `WrapAdapted` to wrap any C# object through its adapter:

```csharp
// Returns a CameraAdapter instance instead of a raw proxy
ZsValueHandle adapted = vm.WrapAdapted(camera);
```

Convention: the ZScript class name must match the C# type name. Override with:

```csharp
vm.AdapterRegistry.Register("UnityEngine.Camera", "CameraAdapter");
```

---

## Exposing C# Types To ZScript With [ZScriptExport]

### The Interface

Any class that implements `IZScriptExport` is auto-discovered when `ZScriptVM.Awake()` runs. No manual registration is needed:

```csharp
public sealed class MyManagerBinding : IZScriptExport
{
    private ZsNativeFn _fnReset;

    public void Register(ZScriptVM vm)
    {
        var tbl = new ZsValueHandle(ZsNative.zs_table_new());

        _fnReset = (_, __, ___) => { MyManager.Reset(); return ZsNative.zs_value_nil(); };
        ZsNative.zs_table_set_fn(tbl.Raw, "Reset", _fnReset, vm.RawVM);

        ZsNative.zs_vm_set_global(vm.RawVM, "MyManager", tbl.Raw);
        tbl.Dispose();
    }
}
```

Inside ZScript:

```ts
fn onButton() {
    MyManager.Reset()
}
```

### Codegen — Generate Export Bindings

For types marked `[ZScriptExport]`, the export generator writes binding code automatically.

Run via **Tools > ZScript > Generate Export Bindings**.

Mark a C# class:

```csharp
[ZScriptExport]
public static class ScoreManager
{
    public static int Score { get; set; }
    public static void AddScore(int amount) { Score += amount; }
    public static event Action<int> OnScoreChanged;
}
```

The generator writes `Assets/ZScriptGenerated/ScoreManager_ZsBinding.cs` which:

- exposes `Score` as `getScore()` / `setScore(value)` table functions
- exposes `AddScore(amount)` as a table function
- exposes `OnScoreChanged` as an event proxy table with `AddListener`, `RemoveListener`, and `RemoveAllListeners`
- stores all delegate references to prevent garbage collection

ZScript usage after generation:

```ts
fn setup() {
    ScoreManager.setScore(0)

    // subscribe to the event
    var id = ScoreManager.onScoreChanged.AddListener(fn(score) {
        Debug.Log("Score: " + score)
    })
}

fn addPoints() {
    ScoreManager.AddScore(10)
}
```

Supported member kinds:

| Member | Supported types |
|--------|----------------|
| `public static` methods | `void`, `bool`, `int`, `long`, `float`, `double`, `string` return and parameters |
| `public static` properties | same primitive types |
| `public static event Action` | no-arg events |
| `public static event Action<T>` | single primitive-typed arg |

Members with unsupported types are skipped with a `// TODO` comment in the generated file.

Use `[ZScriptHide]` to exclude individual members from codegen:

```csharp
[ZScriptExport]
public static class GameManager
{
    public static int Score { get; set; }

    [ZScriptHide]
    public static void InternalReset() { }  // not exposed
}
```

Re-run the tool after changing `[ZScriptExport]` types. Generated files live in `Assets/ZScriptGenerated/` and should be re-generated rather than edited by hand.

---

## Coroutines And Yield Helpers

The Unity bridge uses the runtime coroutine system and helper translation layers to map script-side yield behavior to Unity-friendly coroutine handling.

Available yield helpers from ZScript after `ZScriptVM.Awake()` runs:

```ts
fn myRoutine() {
    coroutine.yield(WaitForSeconds(1.5))
    coroutine.yield(WaitForEndOfFrame())
    coroutine.yield(WaitForFixedUpdate())
    coroutine.yield(WaitUntil(fn() { return ready }))
    coroutine.yield(WaitWhile(fn() { return !done }))
}
```

Start and stop from C#:

```csharp
Coroutine co = vm.StartZsCoroutine("myRoutine");
vm.StopZsCoroutine(co);
```

This is one of the integration surfaces most likely to surface cross-language regressions, which is why EditMode coverage is important.

---

## IL2CPP / AOT Builds

ZScript uses reflection to discover `IZScriptExport` implementations and to access `[ZScriptExport]` types at runtime. These types must be preserved from IL2CPP stripping.

Run **Tools > ZScript > Generate IL2CPP link.xml** to write `Assets/link.xml` automatically. The generated file preserves:

- The entire `ZScript.Runtime` assembly
- The `ZScriptGenerated` namespace (generated bindings)
- All types marked `[ZScriptExport]`
- All `IZScriptExport` implementations

Re-generate `link.xml` whenever `[ZScriptExport]` types or `IZScriptExport` implementations are added or removed before making an IL2CPP build.

---

## Packaging Model

For distributable Unity plugin packages, CI builds per-platform native binaries and then assembles a combined plugin artifact.

This is separate from the test workflow:

- test jobs validate the package on host platforms
- package jobs collect native plugin outputs from multiple runners

## Related Pages

- [[../reference/Annotations, Tags, and Metadata]]
- [[../reference/Language Reference]]
- [[C++ Embedding]]
