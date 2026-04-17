# Overview

ZScript is an embeddable scripting language implemented in modern C++.

At a high level, the project provides:

- a lexer, parser, AST, compiler, and bytecode VM in `src/`
- a command-line tool, `zsc`, for running, checking, compiling, disassembling, and debugging scripts
- a native C API in `src/unity/` for host integration
- a Unity runtime package in `unity/com.zscript.runtime`
- a .NET / NuGet wrapper in `nuget/`
- editor integration via the VS Code extension in `vscode-extension/`

## Major Capabilities

- dynamic scripting runtime with a custom bytecode VM
- module loading and hotpatch support
- native function and class binding from C++
- tag-based conditional compilation with `@tag { ... }`
- coroutines and delegate/event support
- annotation metadata for engine-facing integrations
- Unity package with EditMode tests and native plugin packaging

## Audience

This vault is split into two perspectives:

- public documentation under `documentation/` for users integrating ZScript into their own projects
- a separate private maintainer vault such as `documentation-internal/` for repository operations, release processes, and internal runbooks

## Core Entry Points

- CLI: `src/main.cpp`
- VM runtime: `src/vm.h`, `src/vm.cpp`
- C++ binding helpers: `src/binding.h`
- Unity C API: `src/unity/zscript_c.h`
- Unity package: `unity/com.zscript.runtime`

## Related Pages

- [[Developer Guide]]
- [[getting-started/Quick Start]]
- [[reference/Language Reference]]
