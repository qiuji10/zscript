# Language Reference

This page is the main reference for the ZScript language implemented in this repository today.

ZScript is a dynamically typed language with a custom bytecode VM, classes, traits, annotations, tag-gated compilation, coroutines, and host integration hooks.

## Lexical Structure

### Comments

ZScript supports line comments with `//`.

```ts
// this is a comment
let hp = 100
```

### Identifiers

Identifiers are used for variables, function names, class names, trait names, and annotation segments.

Examples:

```ts
let score = 0
fn spawn_enemy() { }
class PlayerController { }
```

### Keywords

Examples of reserved keywords include:

- `let`
- `var`
- `fn`
- `return`
- `if`
- `else`
- `while`
- `for`
- `in`
- `class`
- `trait`
- `impl`
- `match`
- `self`
- `import`

## Values and Types

The runtime is dynamically typed. Common runtime value categories include:

- `nil`
- `bool`
- `int`
- `float`
- `string`
- `table`
- function / closure
- class prototype
- native function
- delegate
- coroutine
- host-backed userdata / object handle values

Use `type(x)` to inspect a value at runtime.

```ts
print(type(42))
print(type("hello"))
```

## Variables

### Mutable Bindings

Use `var` for mutable bindings:

```ts
var hp = 100
hp = hp - 5
```

### Immutable Bindings

Use `let` for immutable bindings:

```ts
let name = "player"
```

## Functions

Functions are declared with `fn`.

```ts
fn add(a, b) {
    return a + b
}
```

### Default Arguments

```ts
fn repeat(value, count = 3) {
    return string.rep(str(value), count)
}
```

### Lambdas

The language supports lambda expressions and closures.

```ts
let double = fn(x) {
    return x * 2
}
```

Closures capture surrounding values by reference through the VM's upvalue model.

## Expressions

ZScript supports the usual expression categories:

- literals
- arithmetic and comparison operators
- logical operators
- assignment
- field access
- function calls
- table and array literals
- grouping with parentheses
- lambda expressions
- `if` expressions
- range expressions

## Control Flow

### `if`

```ts
if hp <= 0 {
    print("dead")
} else {
    print("alive")
}
```

### `while`

```ts
var i = 0
while i < 3 {
    print(i)
    i = i + 1
}
```

### `for`

`for` supports iteration over ranges and iterable runtime values used by the compiler/runtime.

```ts
for i in 0..<5 {
    print(i)
}
```

### `match`

`match` supports arm-based branching and is compiled directly by the bytecode compiler.

```ts
match state {
    "idle" => print("idle")
    "run" => print("run")
    _ => print("unknown")
}
```

### Errors

ZScript supports `try`, `catch`, and `throw`/`error`.

```ts
try {
    error("boom")
} catch e {
    print("caught", e)
}
```

## Collections

## Arrays

Array literals use bracket syntax.

```ts
let values = [1, 2, 3]
values.push(4)
```

The VM represents arrays as the sequential portion of a table.

## Tables

Tables support associative fields and indexed elements.

```ts
let actor = {
    name: "Enemy",
    hp: 25
}

actor.hp = actor.hp - 5
```

Tables are the core aggregate type used for objects, modules, arrays, and host bridges.

## Destructuring

The parser and compiler support destructuring for array and table patterns.

Use this when unpacking multi-value data structures or structured results.

## Strings

Strings are first-class values. The runtime exposes both a `string` module and method-style dispatch:

```ts
let s = "  zscript  "
print(s.trim().upper())
```

## Classes

Classes are prototype-style runtime objects compiled into class tables plus methods.

```ts
class Vec2 {
    fn init(x, y) {
        self.x = x
        self.y = y
    }

    fn len_sq() {
        return self.x * self.x + self.y * self.y
    }
}

let v = Vec2(3, 4)
print(v.len_sq())
```

### Inheritance

Classes can inherit from a base class.

The compiler copies inherited methods into the subclass prototype and allows subclass methods to override them.

### `self`

Methods receive `self` implicitly through the compiled method calling convention.

## Traits and `impl`

Traits describe shared behavior contracts and may also provide default method bodies.

```ts
trait Walkable {
    fn walk() {
        return "walking"
    }
}

class Dog { }

impl Walkable for Dog { }
```

`impl` also supports inherent implementations that add methods directly to an existing class.

## Generics

ZScript supports generic syntax and type parameter parsing.

```ts
fn identity<T>(x) {
    return x
}

let value = identity<int>(42)
```

Current behavior is pragmatic rather than a full static type system:

- generic syntax is parsed and compiled
- type arguments are available to the runtime in selected cases
- downstream hosts should treat generics as an implemented language feature, but not as a full compile-time type checker

## Operators

The language supports:

- arithmetic: `+`, `-`, `*`, `/`, `%`
- comparison: `==`, `!=`, `<`, `<=`, `>`, `>=`
- assignment: `=`
- delegate-aware assignment operators: `+=`, `-=`
- range operators used in loops and range values
- type / instance checks through `is`

## Optional and Forced Access

Field and call expressions support access variants used by the parser and compiler:

- normal access
- safe / optional access
- force access

These forms are intended for host-friendly scripting where nullability and runtime object traversal are common.

## Modules

Modules are loaded through the VM loader and import system.

```ts
import gameplay.enemy as enemy
```

The exact loading behavior depends on the host VM configuration.

## Delegates

Delegates are first-class runtime values and support additive composition.

```ts
var on_damage = nil
on_damage += fn(value) { print("damage", value) }
on_damage += fn(value) { print("log", value) }
```

The runtime uses delegate-aware behavior for `+=` and `-=`.

This feature is especially important for Unity-style event bridges.

## Coroutines

The runtime exposes a `coroutine` module:

- `coroutine.create`
- `coroutine.status`
- `coroutine.yield`
- `coroutine.resume`
- `coroutine.wrap`

Example:

```ts
let co = coroutine.create(fn() {
    coroutine.yield(1)
    return 2
})

let a = coroutine.resume(co)
let b = coroutine.resume(co)
```

Coroutines are used by the Unity integration for wait helpers and engine-facing async flows.

## Annotations

Annotations attach metadata to declarations, especially classes.

```ts
@unity.component
class PlayerController {
}
```

The compiler stores annotation metadata in chunk metadata, and the VM merges it into an annotation registry that host integrations can query.

## Tags

Tags are compile-time gated blocks using `@tag { ... }`.

```ts
@windows {
    print("windows build")
}
```

Tags are activated by the host or CLI:

```bash
zsc run --tag=windows script.zs
```

See [[Annotations, Tags, and Metadata]] for the exact integration model.

## Standard Modules and Globals

`open_stdlib()` registers:

- core globals such as `print`, `assert`, `type`, `range`
- `math`
- `string`
- `table`
- `json`
- `coroutine`

Additional opt-in modules are registered through:

- `open_io()`
- `open_os()`

See [[Standard Library Reference]] for the detailed command and method surface.

## Host Interop Notes

ZScript is designed to be embedded.

Important host-facing behavior includes:

- native function registration
- native class binding
- annotation queries
- object handle bridging
- hotpatch polling

For embedding details, use:

- [[../integrations/C++ Embedding]]
- [[../integrations/Unity Integration]]

## Related Pages

- [[Standard Library Reference]]
- [[Annotations, Tags, and Metadata]]
- [[../getting-started/CLI]]
- [[../integrations/C++ Embedding]]
