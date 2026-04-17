# C++ Embedding

This page documents the native embedding surface for downstream C++ applications.

The primary public headers are:

- `src/vm.h`
- `src/binding.h`

## Typical Host Responsibilities

A C++ host usually does some combination of:

- create and configure a `zscript::VM`
- open the standard library and any opt-in modules
- load source files or modules
- call script globals from native code
- register native functions
- bind native classes
- poll for hotpatch updates

## Minimal Example

```cpp
#include "vm.h"

int main() {
    zscript::VM vm;
    vm.open_stdlib();
    vm.load_file("hello.zs");
    vm.call_global("main");
}
```

## VM Setup

### Constructing a VM

```cpp
zscript::VM vm;
```

### Opening Libraries

```cpp
vm.open_stdlib();
vm.open_io();   // optional
vm.open_os();   // optional
```

Use `open_io()` and `open_os()` only when your host wants those capabilities exposed to scripts.

## Loading and Executing Code

### Load a File

```cpp
vm.load_file("scripts/gameplay.zs");
```

### Execute Compiled Code

Use the higher-level VM entrypoints such as `execute`, `execute_module`, and `call_global` depending on your host model.

### Import Modules

```cpp
auto module_value = vm.import_module("gameplay.enemy");
```

Module lookup behavior is controlled by the VM loader configuration and module system.

## Calling Script Code

### Call a Named Global

```cpp
auto result = vm.call_global(
    "spawn_enemy",
    { zscript::Value::from_string("grunt") }
);
```

### Call an Arbitrary Callable

For advanced host scenarios, use `call_value` or `invoke_from_native`.

These are useful when the host stores callbacks, delegates, or table-based callables received from script code.

## Globals

### Write a Global

```cpp
vm.set_global("difficulty", zscript::Value::from_int(3));
```

### Read a Global

```cpp
zscript::Value value = vm.get_global("difficulty");
```

## Native Functions

Register native functions with `register_function`.

```cpp
vm.register_function("host_log", [](std::vector<zscript::Value> args) {
    for (const auto& arg : args) {
        std::cout << arg.to_string() << '\n';
    }
    return std::vector<zscript::Value>{};
});
```

Important characteristics:

- arguments arrive as `std::vector<Value>`
- return values are also a `std::vector<Value>`
- throwing or reporting errors should follow the runtime's error model

## Native Class Binding

`src/binding.h` exposes `ClassBuilder<T>` as the main fluent binding helper.

```cpp
struct Vec2 {
    float x = 0;
    float y = 0;

    float len_sq() const {
        return x * x + y * y;
    }
};

zscript::ClassBuilder<Vec2>(vm, "Vec2")
    .constructor<>()
    .property("x", &Vec2::x)
    .property("y", &Vec2::y)
    .method("len_sq", &Vec2::len_sq)
    .commit();
```

Core binding operations include:

- `.constructor<...>()`
- `.method(...)`
- `.property(...)`
- `.commit()`

## Object Handles and Host Objects

The VM also exposes host-object and object-handle helpers used by engine integrations.

This matters when script values need to represent native engine objects without copying them into script-owned structures.

## Annotations and Metadata

If your host uses annotated script classes, query the VM annotation registry after loading scripts.

Typical pattern:

1. compile and execute class declarations
2. call annotation query APIs
3. build host-side registries or factories from those results

## Hotpatch

The VM includes a hotpatch system and file watcher integration.

Typical usage:

```cpp
vm.enable_hotpatch("scripts");

while (running) {
    vm.poll();
    // host update loop
}
```

Use this when you want live script reloading at safe points.

## API Stability

The embedding surface is currently source-level and repository-owned.

If you integrate ZScript directly from source, treat these as the most important compatibility boundaries:

- `src/vm.h`
- `src/binding.h`
- `src/unity/zscript_c.h` if you depend on the C API

## Related Pages

- [[../reference/Language Reference]]
- [[../reference/Standard Library Reference]]
- [[Unity Integration]]
