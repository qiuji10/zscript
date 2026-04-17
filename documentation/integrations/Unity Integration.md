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

## Package Layout

Important package areas include:

- `Runtime/`
- `Editor/`
- `Tests/EditMode/`
- `Plugins/`

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

EditMode tests are intended to validate:

- VM lifecycle in Unity
- annotation and tag behavior through the Unity wrapper
- bridge-layer object and event behavior
- Unity-specific regressions that native tests alone cannot catch

## Annotations In Unity

Unity is one of the main reasons ZScript has first-class annotation metadata.

Typical script patterns:

```ts
@unity.component
class PlayerController {
}
```

or the shorter form used by some tests:

```ts
@component
class MyActor {
}
```

The Unity runtime can query the VM for classes carrying those annotations and then build Unity-facing behavior from that metadata.

## Coroutines And Yield Helpers

The Unity bridge uses the runtime coroutine system and helper translation layers to map script-side yield behavior to Unity-friendly coroutine handling.

This is one of the integration surfaces most likely to surface cross-language regressions, which is why EditMode coverage is important.

## Packaging Model

For distributable Unity plugin packages, CI builds per-platform native binaries and then assembles a combined plugin artifact.

This is separate from the test workflow:

- test jobs validate the package on host platforms
- package jobs collect native plugin outputs from multiple runners

## Related Pages

- [[../reference/Annotations, Tags, and Metadata]]
- [[../reference/Language Reference]]
- [[C++ Embedding]]
