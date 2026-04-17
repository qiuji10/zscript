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

## Unity-Specific Usage

The Unity runtime uses annotations and helper lookups to discover interesting ZScript classes.

Examples include component-like or engine-specific metadata such as:

- `@component`
- `@unity.component`

See [[../integrations/Unity Integration]] for the higher-level Unity-facing workflow.

## Design Notes

- tags affect compilation
- annotations survive as runtime metadata
- tags are selected by the active build / host context
- annotations are selected by the host's discovery logic

Those roles are complementary and should not be mixed together.

## Related Pages

- [[Language Reference]]
- [[Standard Library Reference]]
- [[../getting-started/CLI]]
- [[../integrations/Unity Integration]]
