# Developer Guide

This page is the public starting point for developers who want to evaluate, adopt, and integrate ZScript in their own applications or engines.

## What ZScript Provides

- a native scripting runtime in C++
- a CLI for running and compiling scripts
- an embeddable C++ API
- a C API for host integrations
- a Unity runtime package
- a managed .NET package for native interop consumers

## Where to Start

- build and run your first script: [[getting-started/Quick Start]]
- learn the command surface: [[getting-started/CLI]]
- learn the language: [[reference/Language Reference]]
- learn the runtime library: [[reference/Standard Library Reference]]
- embed in a native host: [[integrations/C++ Embedding]]
- integrate with Unity: [[integrations/Unity Integration]]
- understand the supported integration surfaces through [[integrations/C++ Embedding]] and [[integrations/Unity Integration]]

## Who ZScript Is For

ZScript is a strong fit when you want:

- an embeddable scripting runtime rather than a standalone application platform
- a native C++ implementation you can build with your engine or tools
- first-class host integration points such as native function binding, metadata, and object bridging
- a Unity package that shares the same underlying runtime

## Main Adoption Paths

### Evaluate the Language

Start here if you need to understand syntax, runtime behavior, or the standard library before choosing an integration path:

- [[reference/Language Reference]]
- [[reference/Standard Library Reference]]
- [[reference/Annotations, Tags, and Metadata]]

## Supported Integration Paths

### Native C++

Best for:

- game engines
- tools
- applications embedding the runtime directly

Primary headers:

- `src/vm.h`
- `src/binding.h`

Primary docs:

- [[integrations/C++ Embedding]]

### Unity

Best for:

- Unity projects that need a scripting runtime inside the Editor or player

Primary package:

- `unity/com.zscript.runtime`

Primary docs:

- [[integrations/Unity Integration]]

### Managed / .NET

Best for:

- .NET consumers that want a packaged wrapper around the native library

Primary project:

- `nuget/ZScript.Native.csproj`

## Related Pages

- [[getting-started/Quick Start]]
- [[getting-started/CLI]]
- [[reference/Language Reference]]
- [[reference/Standard Library Reference]]
- [[integrations/C++ Embedding]]
- [[integrations/Unity Integration]]
- [[integrations/C++ Embedding]]
