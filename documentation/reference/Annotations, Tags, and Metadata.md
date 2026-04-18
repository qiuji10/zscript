# Annotations, Tags, and Metadata

ZScript has two related but distinct metadata systems:

- annotations, which attach metadata to declarations
- tags, which gate source blocks at compile time

Both are heavily used by host integrations, especially Unity.

## Annotations

Annotations use `@` syntax attached to declarations, primarily classes.

Examples:

```ts
@component
class MyActor {
}
```

```ts
@unity.component
class PlayerController {
}
```

```ts
@unreal.uclass
@unreal.blueprintable
class Actor {
}
```

### Annotation Shape

Annotations are compiled as namespace-plus-name metadata:

- `@component` becomes namespace `component`, empty name
- `@unity.component` becomes namespace `unity`, name `component`

The compiler stores them in chunk metadata rather than as user-editable script fields.

### Runtime Registry

When code is executed, annotation metadata is merged into the VM annotation registry.

Important properties of this design:

- script code does not own the authoritative annotation registry
- annotations are not meant to be mutated from script
- hosts can query annotations reliably after compilation / execution

This is why `__annotations__` is not the source of truth for host integrations.

---

## Unity-Defined Annotations

These annotations are defined by convention and consumed by the Unity runtime package. They have no special meaning to the ZScript compiler itself.

### @unity.component

Marks a ZScript class as a Unity component. The Unity runtime discovers classes with this annotation to build component pickers and support `ZScriptVM.AttachComponent`.

```ts
@unity.component
class PlayerController {
    fn Start()     { }
    fn Update()    { }
    fn OnDestroy() { }
}
```

Query from C#:

```csharp
string[] classes = vm.GetComponentClasses();
// or
string[] classes = vm.FindAnnotatedClasses("unity", "component");
```

### @unity.serialize

Marks a ZScript class as having Unity inspector-editable fields. A `ZsSerializedFields` MonoBehaviour component stores the inspector values; `ZsComponentBridge` injects them into the ZScript instance before `Start()` is called.

Field names in the Inspector must match ZScript field names exactly (case-sensitive).

```ts
@unity.serialize
@unity.component
class Enemy {
    var hp    = 100
    var speed = 3.5
    var label = "enemy"

    fn Start() {
        // Instance fields should be read through self.
        // hp, speed, and label are already set from the Inspector by the time
        // Start() runs.
        Debug.Log("hp=" + self.hp)
    }
}
```

Supported field types: `Bool`, `Int`, `Float`, `String`.

Query from C# (to check whether a class is serializable):

```csharp
string[] annotations = vm.GetClassAnnotations("Enemy");
// annotations contains "unity.serialize" and "unity.component"
```

### @unity.adapter

Marks a ZScript class as a typed adapter for a C# object. An adapter wraps a raw C# proxy and provides a clean ZScript API. The adapter's constructor receives the C# proxy as its first argument.

Convention: the ZScript class name must match the C# type's simple name.

```ts
@unity.adapter
class Rigidbody {
    fn new(proxy) { self._rb = proxy }

    fn velocity()          { return self._rb.velocity }
    fn addForce(x, y, z)   { self._rb.AddForce(x, y, z) }
}
```

After `ZScriptVM.Start()` runs:

```csharp
// Returns a Rigidbody adapter wrapping the C# Rigidbody, not a raw proxy
ZsValueHandle adapted = vm.WrapAdapted(rigidBody);
```

Manually map type names when class names diverge:

```csharp
vm.AdapterRegistry.Register("UnityEngine.Rigidbody", "Rigidbody");
```

---

## Tags

Tags are compile-time conditional blocks:

```ts
@windows {
    print("windows-specific code")
}

@unity {
    print("unity-only code")
}
```

Only blocks with active tags survive compilation.

### Activating Tags

From the CLI:

```bash
zsc run --tag=windows --tag=unity game.zs
```

From embedding hosts:

- add tags to the VM before compilation or execution
- Unity and other hosts can use tags to select engine-specific code paths

### Typical Uses

- platform-specific logic such as `@windows` or `@macos`
- engine-specific branches such as `@unity`
- scenario or build-flavor blocks such as `@demo`, `@server`, or `@editor`

---

## Host Query APIs

Hosts can query the annotation registry after script load.

Important host surfaces include:

- C++ VM annotation getters
- Unity C API query functions in `src/unity/zscript_c.h`
- Unity C# wrappers that expose class annotation lookups

Common integration pattern:

1. load script source
2. compile and execute declarations
3. ask the host bridge for classes carrying a particular annotation
4. instantiate or bind host behavior based on those results

### Unity C# Query Examples

```csharp
// All classes with @unity.component
string[] components = vm.GetComponentClasses();

// All classes with a custom annotation
string[] serializable = vm.FindAnnotatedClasses("unity", "serialize");

// All annotations on a specific class
string[] annotations = vm.GetClassAnnotations("PlayerController");
// e.g. ["unity.component", "unity.serialize"]
```

---

## Design Notes

- tags affect compilation
- annotations survive as runtime metadata
- tags are selected by the active build / host context
- annotations are selected by the host's discovery logic

Those roles are complementary and should not be mixed together.

The Unity-defined annotations (`@unity.component`, `@unity.serialize`, `@unity.adapter`) are conventions. The ZScript compiler treats them the same as any other annotation; the Unity runtime package gives them their specific meaning.

## Related Pages

- [[Language Reference]]
- [[Standard Library Reference]]
- [[../getting-started/CLI]]
- [[../integrations/Unity Integration]]
