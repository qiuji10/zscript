# Custom Scripting Language — Design Plan

## Name Placeholder: `ZScript` (rename as needed)

---

## 1. Goals

- Embeddable scripting language running on top of native C++
- First-class **hotpatch** (reload scripts at runtime without restart)
- Cross-platform: Windows, Linux, macOS, iOS, Android
- Game engine bindings: **Unity** (C# interop via IL2CPP/native plugin) and **Unreal** (C++ plugin)
- Syntax inspired by Lua/Python (simple, readable)

---

## 2. Architecture Overview

```
┌────────────────────────────────────────────────────┐
│                   Host Application                 │
│  (C++ native / Unity C# / Unreal C++)              │
│                                                    │
│   ┌────────────┐    ┌──────────────────────────┐   │
│   │ ZScript VM │◄───│   Binding Layer (C API)  │   │
│   │            │    │  register funcs/classes  │   │
│   └─────┬──────┘    └──────────────────────────┘   │
│         │                                          │
│   ┌─────▼──────┐                                   │
│   │ Hotpatch   │  FileWatcher → recompile → swap   │
│   │ Manager    │  module in VM without restart      │
│   └────────────┘                                   │
└────────────────────────────────────────────────────┘
```

---

## 3. Core Components

### 3.1 Lexer & Parser
- Hand-written recursive-descent parser (no Flex/Bison dependency)
- Produces an **AST** (Abstract Syntax Tree)
- Error recovery: continue parsing after syntax errors, collect all errors

### 3.2 Compiler (AST → Bytecode)
- Custom bytecode (register-based VM, like Lua 5)
- Bytecode is serializable → `.zbc` files (for shipping compiled scripts)
- Optional: source maps for debugging

### 3.3 Virtual Machine (VM)
- Register-based interpreter written in C++17
- Stack for call frames, heap for GC'd objects
- **Module system**: each script file = one module, independently loadable/unloadable

### 3.4 Garbage Collector
- Simple **incremental mark-and-sweep** to start
- Later option: generational GC
- Manual `ref`/`unref` available for C++ side to pin objects

### 3.5 Type System
- Dynamic typing (like Lua/Python) — simplest to start
- Optional static type hints for tooling (no runtime enforcement initially)
- Core types: `nil`, `bool`, `int`, `float`, `string`, `table`, `function`, `userdata`

### 3.6 Standard Library (minimal)
- `math`, `string`, `table`, `io` (sandboxed), `os` (sandboxed)
- No stdlib dependency the host doesn't approve → whitelist approach

---

## 4. Hotpatch System

This is the most critical feature. Strategy:

### 4.1 Module Versioning
- Each module has a **version counter**
- On reload: recompile → produce new bytecode → swap module pointer atomically
- Old module stays alive until all running call frames referencing it finish

### 4.2 State Preservation
- Global variables in a module are **migrated** to new version by name
- Functions are replaced; closures referencing old upvalues get a migration hook
- Host can register `on_reload(old_state) → new_state` callbacks per module

### 4.3 File Watcher
- Platform abstraction over:
  - Windows: `ReadDirectoryChangesW`
  - Linux: `inotify`
  - macOS: `FSEvents`
  - iOS/Android: polling fallback (filesystem restrictions)
- Debounce: wait 50ms after last change before triggering recompile

### 4.4 Thread Safety
- VM runs on main thread (or dedicated script thread)
- FileWatcher runs on background thread
- Hotpatch queues reload request → applied at a **safe point** (between frames or explicit `vm.poll()`)

---

## 5. Language Syntax

### 5.1 Unreal-style — Actor

```zscript
@unreal.uclass
class PlayerActor : Actor {

    @unreal.uproperty(EditAnywhere)
    var Speed: Float = 600

    fn BeginPlay() {
        log("Player spawned")
    }

    fn Tick(deltaTime: Float) {
        let dir = input.getAxis("MoveForward")
        let pos = self.GetActorLocation()

        self.SetActorLocation(
            pos + Vec3(dir * Speed * deltaTime, 0, 0)
        )
    }
}
```

### 5.2 Unreal-style — Component

```zscript
@unreal.uclass(BlueprintSpawnableComponent)
class HealthComponent : ActorComponent {

    @unreal.uproperty(EditAnywhere)
    var MaxHp: Int = 100

    var CurrentHp: Int = 100

    fn ApplyDamage(amount: Int) {
        CurrentHp = max(0, CurrentHp - amount)

        if CurrentHp == 0 {
            self.GetOwner()?.Destroy()
        }
    }
}
```

### 5.3 Unreal-style — Direct engine API

```zscript
fn Test() {
    let world = World.get()
    let actor = world.SpawnActor(PlayerActor, Vec3(0, 0, 100))
    actor.SetActorLocation(Vec3(100, 0, 50))
}
```

### 5.4 Unity-style — MonoBehaviour

```zscript
@unity.component
class PlayerController : MonoBehaviour {

    @unity.serialize
    var Speed: Float = 5.0

    fn Start() {
        log("Player started")
    }

    fn Update() {
        let h = input.getAxis("Horizontal")
        let v = input.getAxis("Vertical")

        let move = Vec3(h, 0, v) * Speed * Time.deltaTime

        self.transform.position += move
    }
}
```

### 5.5 Unity-style — Component access

```zscript
@unity.component
class JumpController : MonoBehaviour {

    var rb: Rigidbody

    fn Start() {
        rb = self.GetComponent<Rigidbody>()
    }

    fn Update() {
        if input.keyDown("Space") {
            rb.AddForce(Vec3(0, 300, 0))
        }
    }
}
```

### 5.6 Cross-engine — trait + impl adapter

```zscript
// Define common interface (shared)
trait Movable {
    prop position: Vec3
    fn translate(delta: Vec3)
}

// Unity adapter
@unity.adapter
impl Movable for Transform {
    prop position {
        get => self.position
        set(v) => self.position = v
    }
    fn translate(delta: Vec3) {
        self.position += delta
    }
}

// Unreal adapter
@unreal.adapter
impl Movable for Actor {
    prop position {
        get => self.GetActorLocation()
        set(v) => self.SetActorLocation(v)
    }
    fn translate(delta: Vec3) {
        self.SetActorLocation(self.GetActorLocation() + delta)
    }
}

// Shared gameplay code — runs on both engines
fn MoveForward(obj: Movable, speed: Float, dt: Float) {
    obj.translate(Vec3(speed * dt, 0, 0))
}
```

### 5.7 Mixed engine-specific blocks inside shared function

```zscript
fn UpdatePlayer(player: Movable) {
    let dt = Time.deltaTime

    MoveForward(player, 10, dt)      // shared

    @unity {
        player.GetComponent<Rigidbody>()?.AddForce(Vec3(0, 1, 0))
    }

    @unreal {
        player.AddMovementInput(Vec3(0, 1, 0))
    }
}
```

### 5.8 Events / Delegates

```zscript
// Unreal style
actor.OnActorBeginOverlap += fn(other: Actor) {
    log("Hit: {other.Name}")
}

// Unity style
button.onClick += fn() {
    log("Clicked")
}

// Removable delegate (store handle)
let handle = fn(other: Actor) { log("hit") }
actor.OnActorBeginOverlap += handle
actor.OnActorBeginOverlap -= handle
```

### 5.9 Spawning

```zscript
fn SpawnEnemies() {
    for let i in 0..<10 {
        let pos = Vec3(i * 100, 0, 0)

        @unreal {
            World.get().SpawnActor(Enemy, pos)
        }

        @unity {
            instantiate(enemyPrefab, pos)
        }
    }
}
```

### 5.10 AI / Gameplay logic

```zscript
class AIController {

    var target: Actor?

    fn update(dt: Float) {
        if target == nil { return }

        let pos       = self.GetActorLocation()
        let targetPos = target!.GetActorLocation()
        let dir       = (targetPos - pos).normalized()

        self.AddMovementInput(dir)
    }
}
```

### 5.11 Hot-reload hook (optional per module)

```zscript
fn on_reload(old: Any) -> Any {
    // migrate state from previous version
    return old
}
```

### Key syntax rules (locked)
- `{}` blocks — no `end`, no indentation requirement
- `let` = immutable binding always, `var` = mutable binding always
- `fn` for functions, explicit return type required: `fn foo() -> Vec3 {}`
- `@unreal {}` / `@unity {}` blocks stripped at compile time based on `vm.setEngine()`
- `T` = non-null, `T?` = nullable, `?.` safe call, `!.` force unwrap
- Generics: `<T>` with lookahead disambiguation
- String interpolation: `"Hit: {other.Name}"`

---

## 6. C++ Embedding API

```cpp
// Initialize VM
ZScript::VM vm;
vm.open_stdlib();

// Register a C++ function
vm.register_function("engine.log", [](ZScript::State& s) {
    std::cout << s.arg<std::string>(0) << "\n";
    return 0;
});

// Register a C++ class
vm.register_class<RigidBody>("RigidBody")
    .method("apply_force", &RigidBody::apply_force)
    .property("mass",      &RigidBody::mass);

// Load and run a script
vm.load_file("player.zs");
vm.call("Player.new");

// Enable hotpatch (watches "scripts/" folder)
vm.enable_hotpatch("scripts/");

// Game loop
while (running) {
    vm.poll();          // apply pending hotpatches here
    vm.call("update");
}
```

---

## 7. Platform Matrix

| Platform | File Watch     | Compilation Thread | Notes                        |
|----------|----------------|--------------------|------------------------------|
| Windows  | WinAPI         | std::thread        | Full support                 |
| Linux    | inotify        | std::thread        | Full support                 |
| macOS    | FSEvents       | std::thread        | Full support                 |
| iOS      | Poll fallback  | std::thread        | Sandboxed FS, limited paths  |
| Android  | Poll fallback  | std::thread        | APK assets → extract first   |

---

## 8. Binding & Registration System

### 8.1 Core Principle — No Reflection Required
Reflection is **never required** by the VM. It is an optional convenience layer.
The VM only needs to know what the host explicitly tells it — everything is registered manually via a C++ API.

### 8.2 Foundation — Manual Registration
Always works. Zero dependencies. IL2CPP/AOT safe. The binding layer is a fluent C++ API:

```cpp
// Register a free function
vm.register_function("engine.log", [](ZScript::State& s) {
    std::cout << s.arg<std::string>(0) << "\n";
    return 0;
});

// Register a class with methods and properties
vm.register_class<Player>("Player")
    .constructor<std::string>()        // Player.new("Hero")
    .method("take_damage", &Player::take_damage)
    .method("die",         &Player::die)
    .property("health",    &Player::health)
    .property("name",      &Player::name);
```

This is the **only mandatory path**. Everything below is optional ergonomics on top.

### 8.3 Optional — Macro Helpers (reduce boilerplate)
A set of macros that expand to the exact same manual registration calls above.
No magic — just less typing for large codebases.

```cpp
// zscript_bind.h included in your class file

ZSCRIPT_CLASS(Player)
    ZSCRIPT_CTOR(std::string)
    ZSCRIPT_METHOD(take_damage)
    ZSCRIPT_METHOD(die)
    ZSCRIPT_PROPERTY(health)
    ZSCRIPT_PROPERTY(name)
ZSCRIPT_END()

// Then in your VM setup, one line:
ZScript::register_all(vm);   // calls everything declared via macros
```

Macros use a static registration pattern (self-registering via global initializers) so
you never need a central list — adding the macro to a class file is enough.

### 8.4 Optional — Engine-Specific Auto-Binding

| Engine | Strategy | Result |
|---|---|---|
| **Unreal** | Read existing `UCLASS / UPROPERTY / UFUNCTION` metadata | Near-automatic binding for any `UObject` subclass |
| **Unity** | Offline codegen from C# `[ZScriptExport]` attribute → generates C binding `.cpp` | No runtime reflection, IL2CPP safe |
| **Raw C++** | Clang tooling parses headers, emits registration `.cpp` files | Optional, for very large codebases |

These are **Phase 5** additions — the core VM and manual registration ship first.

### 8.5 Decision: What Binding Path to Use

```
Is your project small-to-medium?
  └─ Yes → Manual registration. Simple, explicit, done.

Is it large with many classes?
  └─ Using Unreal? → Piggyback on UObject reflection (auto)
  └─ Using Unity?  → Add [ZScriptExport] + run codegen
  └─ Raw C++?      → Use ZSCRIPT_CLASS macros
```

---

## 9. Game Engine Integration

### 8.1 Unreal Engine
- Delivered as a **UE Plugin** (`ZScriptPlugin`)
- `UZScriptSubsystem` (Game Instance Subsystem) owns the VM
- Blueprint node: `Call ZScript Function`
- Live in editor: hotpatch triggers on script save in editor
- Unreal object binding: expose `UObject` subclasses via reflection macro

### 8.2 Unity
- Delivered as a **Native Plugin** (`.dll` / `.so` / `.dylib`)
- C API wrapper → P/Invoke from C#
- `ZScriptVM` MonoBehaviour component owns the VM
- Editor extension: auto-reload scripts on file change in Play mode
- IL2CPP compatible (no managed reflection at runtime, pure C API)

---

## 10. Tooling (Phase 2)

- **CLI compiler**: `zsc compile player.zs -o player.zbc`
- **LSP server**: autocomplete, go-to-definition, inline errors (VS Code extension)
- **Debugger protocol**: DAP (Debug Adapter Protocol) — breakpoints, step, inspect
- **Bytecode disassembler**: `zsc disasm player.zbc`

---

## 11. Phased Roadmap

| Phase | Deliverable                                                   |
|-------|---------------------------------------------------------------|
| 1     | Lexer + Parser + AST                                         |
| 2     | Bytecode compiler + register VM (no GC yet, no hotpatch)     |
| 3     | GC + module system + C++ binding API                         |
| 4     | Hotpatch system + FileWatcher                                |
| 5     | Unreal plugin + Unity plugin                                 |
| 6     | Tooling: CLI, LSP, debugger                                  |

---

## 12. Key Technical Decisions

| Decision             | Options                          | Status         | Locked Choice                  |
|----------------------|----------------------------------|----------------|-------------------------------|
| VM type              | Stack-based vs Register-based    | ✅ Locked       | Register-based (Lua-proven)   |
| Block syntax         | `end` vs `{}`                    | ✅ Locked       | `{}` — C#/Swift/Kotlin style  |
| Mutability           | Dynamic / Gradual / Explicit     | ✅ Locked       | `let` = immutable, `var` = mutable (always, no exceptions) |
| Generics             | `<T>` vs `[T]` vs `::T`         | ✅ Locked       | `<T>` with lookahead disambiguation |
| Null safety          | Runtime vs Type-system           | ✅ Locked       | Type-system level — `T` non-null, `T?` nullable |
| Type inference       | Full vs Local-only               | ✅ Locked       | Local-only (like C# `var`), return types must be explicit |
| Delegates            | Value-type vs Reference objects  | ✅ Locked       | Reference objects — supports `+=` / `-=` with stored handle |
| Engine mode          | File-level / Runtime / VM-init   | ✅ Locked       | Host sets at VM init: `vm.setEngine(Engine::Unreal)` |
| GC algorithm         | Mark-sweep vs Ref-count          | 🔲 Pending      | Mark-sweep recommended         |
| Hotpatch granularity | File vs Function                 | 🔲 Pending      | File-level first               |
| Bytecode portability | Binary vs text IR                | 🔲 Pending      | Binary + magic header          |

---

## 12a. Language Rules (Locked)

### Variables
```zscript
let x = 10       // immutable binding — cannot be reassigned, ever
var y = 20       // mutable binding — can be reassigned

let v = Vec3(0,0,0)
v.x = 10         // ✅ object mutation allowed
v = Vec3(1,1,1)  // ❌ rebinding not allowed
```

### Class Fields
```zscript
class Player {
    let id: Int = 1      // readonly after init
    var hp: Int = 100    // mutable
}
player.hp = 50   // ✅
player.id = 2    // ❌ compile error
```

### Function Parameters — immutable by default
```zscript
fn move(dir: Vec3) { dir = Vec3(0,0,0) }      // ❌
fn move(mut dir: Vec3) { dir = dir.normalized() }  // ✅
```

### Generics — C# style with lookahead
```zscript
self.GetComponent<Rigidbody>()   // ✅ generic call
List<Int>()                      // ✅ generic type
if a < b > c { }                 // ❌ compile error — use (a < b) > c
```

### Null safety
```zscript
var a: Actor    // non-null, guaranteed
var b: Actor?   // nullable
b?.Destroy()    // safe call
b!.Destroy()    // force unwrap — crashes if null
```

### Delegates
```zscript
let handle = fn(other: Actor) { log("hit") }
actor.OnBeginOverlap += handle   // ✅ can remove later
actor.OnBeginOverlap -= handle   // ✅
actor.OnBeginOverlap += fn() { } // inline — cannot remove
```

### Engine-conditional blocks (stripped at compile time)
```zscript
@unity   { player.GetComponent<Rigidbody>()?.AddForce(Vec3(0,10,0)) }
@unreal  { player.AddMovementInput(Vec3(0,1,0)) }
```

### Return types — must be explicit
```zscript
fn getLocation() -> Vec3 { return self.GetActorLocation() }  // ✅
fn getLocation() { return self.GetActorLocation() }          // ❌
```

---

## 13. Dependencies (intentionally minimal)

- C++17 only (no Boost, no heavy deps)
- Optional: `re2` or hand-rolled regex for string stdlib
- Optional: `utf8proc` for proper unicode strings
- No LLVM (JIT is Phase 3+ stretch goal)
