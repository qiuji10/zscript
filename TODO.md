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
- [x] Compile `@unity` / `@unreal` blocks: strip based on `vm.setEngine()` setting
- [x] Compile string interpolation to concat ops
- [ ] Compile delegate `+=` / `-=` to delegate object calls
- [x] Emit source maps (optional, for debugger)
- [x] Compile-time error: reassigning `let` binding
- [ ] Compile-time error: missing explicit return type on `fn`
- [ ] Compile-time error: mutable param without `mut` keyword

### Virtual Machine
- [x] Register-based interpreter loop
- [x] Call frame stack
- [x] Core type representation: `nil`, `bool`, `int`, `float`, `string`, `table`, `function`, `userdata`
- [ ] String interning
- [x] Table (hash map) implementation
- [x] Function objects + closures (upvalue capture)
- [x] `vm.setEngine(Engine::Unreal | Engine::Unity)` — engine mode at init
- [ ] `vm.load_file(path)` — load and execute a `.zs` script
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
- [ ] Delegate `+=` / `-=` event subscription / unsubscription
- [x] String built-in methods: already implemented — see stdlib `string` table
- [x] String multiline / raw literals — backtick strings `` `hello\nworld` `` (no escape processing, no interpolation, multiline)
- [x] Range type and range iteration (`0..10`, `0..<10`) — first-class range values via `NewRange`/`NewRangeExcl` opcodes; `TLen` and `GetIndex` handle them transparently; passable to functions
- [ ] Generics / type parameters (syntax parsed; semantic checking not yet implemented)

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
- [-] LSP server skeleton (stdio transport) — `src/lsp.cpp` / `src/lsp.h` exist as stubs
- [ ] `textDocument/completion` — autocomplete identifiers, methods, types
- [ ] `textDocument/definition` — go-to-definition
- [ ] `textDocument/hover` — type info on hover
- [ ] `textDocument/publishDiagnostics` — inline errors
- [ ] VS Code extension scaffold

### Debugger (DAP)
- [-] DAP server skeleton — `src/dap.cpp` / `src/dap.h` exist as stubs
- [ ] Breakpoint set/clear
- [ ] Step over / step into / step out
- [ ] Variable inspection (locals, upvalues, globals)
- [ ] Stack frame display

---

## Ongoing / Cross-cutting

- [-] Standard library
  - [x] `print`, `to_string`, `to_int`, `to_float`, `type_of`, `len` / `#` operator
  - [x] `math`: `floor`, `ceil`, `sqrt`, `abs`, `min`, `max`, `pow`, `round`
  - [x] Array methods (see Language Features above)
  - [x] Table methods (see Language Features above)
  - [x] `string`: `len`, `sub`, `upper`, `lower`, `trim`, `trim_start`, `trim_end`, `contains`, `starts_with`, `ends_with`, `find`, `replace`, `split`, `join`, `rep`, `byte`, `char`, `format`, `is_empty`, `reverse`
  - [x] `json`: `encode(value) -> string`, `decode(string) -> value` (handles nil, bool, int, float, string, arrays, objects; round-trips)
  - [ ] `io` / `os`: sandboxed file and process access
- [ ] Whitelist approach: stdlib only available if host approves
- [x] Test suite: one test file per language feature (lexer, parser, vm, operators, control_flow, functions, classes, collections, closures, error_handling, stdlib, gc, hotpatch)
- [ ] Fuzzer for lexer + parser
- [ ] CI: build matrix (Windows / Linux / macOS)
- [x] Decide: GC algorithm — mark-sweep (implemented)
- [x] Decide: hotpatch granularity — file-level (implemented)
- [x] Decide: bytecode portability — binary + magic header (implemented)
