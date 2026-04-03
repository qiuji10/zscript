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
- [ ] AST pretty-printer (for debugging)
- [x] Source location attached to every node (for error messages + source maps)

---

## Phase 2 — Bytecode Compiler + VM (no GC, no hotpatch)

### Bytecode
- [ ] Design instruction set (register-based, Lua 5 style)
- [ ] Define binary `.zbc` format with magic header
- [ ] Implement bytecode serializer / deserializer
- [ ] Implement bytecode disassembler (`zsc disasm`)

### Compiler (AST → Bytecode)
- [ ] Symbol table + scope resolution
- [ ] Compile expressions to register operations
- [ ] Compile variable declarations (`let` immutability enforced at compile time)
- [ ] Compile function calls including generic calls
- [ ] Compile class instantiation and method dispatch
- [ ] Compile null safety: `?.` emits nil-check branch, `!.` emits force-unwrap + trap
- [ ] Compile `@unity` / `@unreal` blocks: strip based on `vm.setEngine()` setting
- [ ] Compile string interpolation to concat ops
- [ ] Compile delegate `+=` / `-=` to delegate object calls
- [ ] Emit source maps (optional, for debugger)
- [ ] Compile-time error: reassigning `let` binding
- [ ] Compile-time error: missing explicit return type on `fn`
- [ ] Compile-time error: mutable param without `mut` keyword

### Virtual Machine
- [ ] Register-based interpreter loop
- [ ] Call frame stack
- [ ] Core type representation: `nil`, `bool`, `int`, `float`, `string`, `table`, `function`, `userdata`
- [ ] String interning
- [ ] Table (hash map) implementation
- [ ] Function objects + closures (upvalue capture)
- [ ] `vm.setEngine(Engine::Unreal | Engine::Unity)` — engine mode at init
- [ ] `vm.load_file(path)` — load and execute a `.zs` script
- [ ] `vm.call(name, args...)` — call a named script function from C++

---

## Phase 3 — GC + Module System + C++ Binding API

### Garbage Collector
- [ ] Incremental mark-and-sweep GC
- [ ] Write barrier for incremental correctness
- [ ] `ref` / `unref` API for C++ side to pin objects
- [ ] GC tuning knobs (step size, pause threshold)
- [ ] (Stretch) Generational GC upgrade path

### Module System
- [ ] Each `.zs` file = one independently loadable module
- [ ] Module registry (name → compiled module)
- [ ] `import` statement in language
- [ ] Module load / unload lifecycle hooks
- [ ] Circular import detection

### C++ Binding API
- [ ] `ZScript::State` — argument access helpers (`s.arg<T>(n)`, return helpers)
- [ ] `vm.register_function(name, lambda)` — free function registration
- [ ] `vm.register_class<T>(name)` fluent builder:
  - [ ] `.constructor<Args...>()`
  - [ ] `.method(name, &T::method)`
  - [ ] `.property(name, &T::field)`
- [ ] `userdata` wrapper: C++ object lifecycle tied to GC
- [ ] Error propagation: C++ exceptions → ZScript runtime errors

### Macro Helper Layer (optional ergonomics)
- [ ] `ZSCRIPT_CLASS` / `ZSCRIPT_METHOD` / `ZSCRIPT_PROPERTY` / `ZSCRIPT_END` macros
- [ ] Self-registering static initializer pattern
- [ ] `ZScript::register_all(vm)` — bulk registration entry point

---

## Phase 4 — Hotpatch System + FileWatcher

### File Watcher
- [ ] Platform abstraction interface (`IFileWatcher`)
- [ ] Windows: `ReadDirectoryChangesW` backend
- [ ] Linux: `inotify` backend
- [ ] macOS: `FSEvents` backend
- [ ] iOS / Android: polling fallback (configurable interval)
- [ ] 50ms debounce after last change before firing recompile

### Hotpatch Manager
- [ ] Module version counter
- [ ] Background recompile on file change
- [ ] Atomic module pointer swap (new bytecode in, old bytecode kept alive)
- [ ] Old module stays alive until all call frames referencing it drain
- [ ] Global variable migration by name (old → new module version)
- [ ] Closure upvalue migration hook
- [ ] `on_reload(old: Any) -> Any` callback per module (user-defined)
- [ ] `vm.enable_hotpatch(dir)` — watch a directory
- [ ] `vm.poll()` — apply queued hotpatches at safe point (between frames)
- [ ] Thread safety: FileWatcher thread enqueues, VM main thread drains at `poll()`

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
- [ ] Native plugin build: `.dll` (Windows), `.so` (Linux/Android), `.dylib` (macOS/iOS)
- [ ] C API wrapper (`zscript_c.h`) for P/Invoke
- [ ] `ZScriptVM` MonoBehaviour — owns the VM, calls `poll()` in `Update()`
- [ ] Editor extension: auto-reload scripts on file change in Play mode
- [ ] `[ZScriptExport]` C# attribute + offline codegen → produces C binding `.cpp`
- [ ] IL2CPP compatibility audit (no managed reflection at runtime)
- [ ] `@unity.component` / `@unity.serialize` / `@unity.adapter` annotation handling

---

## Phase 6 — Tooling

### CLI (`zsc`)
- [ ] `zsc compile <file.zs> -o <file.zbc>` — compile to bytecode
- [ ] `zsc run <file.zs>` — compile + execute
- [ ] `zsc disasm <file.zbc>` — disassemble bytecode
- [ ] `--engine=unreal|unity|none` flag (sets engine mode for conditional stripping)
- [ ] Human-readable error output with line/column

### LSP Server
- [ ] LSP server skeleton (stdio transport)
- [ ] `textDocument/completion` — autocomplete identifiers, methods, types
- [ ] `textDocument/definition` — go-to-definition
- [ ] `textDocument/publishDiagnostics` — inline errors
- [ ] VS Code extension scaffold

### Debugger (DAP)
- [ ] DAP server skeleton
- [ ] Breakpoint set/clear
- [ ] Step over / step into / step out
- [ ] Variable inspection (locals, upvalues, globals)
- [ ] Stack frame display

---

## Ongoing / Cross-cutting

- [ ] Standard library: `math`, `string`, `table`, `io` (sandboxed), `os` (sandboxed)
- [ ] Whitelist approach: stdlib only available if host approves
- [ ] Test suite: one test file per language feature
- [ ] Fuzzer for lexer + parser
- [ ] CI: build matrix (Windows / Linux / macOS)
- [ ] Decide: GC algorithm (mark-sweep vs ref-count) — leaning mark-sweep
- [ ] Decide: hotpatch granularity (file-level first, function-level later)
- [ ] Decide: bytecode portability (binary + magic header)
