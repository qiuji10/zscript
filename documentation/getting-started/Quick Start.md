# Quick Start

This page is the fastest path from clone to first working ZScript program.

## What You Get

ZScript provides:

- a native compiler and bytecode VM
- a CLI called `zsc`
- an embeddable C++ runtime
- a Unity package and native plugin bridge

## Build the CLI

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Main outputs:

- `build/zsc` on single-config generators
- `build/Release/zsc.exe` on multi-config Windows generators

## Run a Script

Create a file named `hello.zs`:

```ts
fn main() {
    print("hello from zscript")
}

main()
```

Run it:

```bash
./build/zsc hello.zs
```

Equivalent explicit form:

```bash
./build/zsc run hello.zs
```

## Check Syntax Without Running

```bash
./build/zsc check hello.zs
```

## Compile to Bytecode

```bash
./build/zsc compile hello.zs -o hello.zbc
./build/zsc disasm hello.zbc
```

## Minimal Language Example

```ts
class Counter {
    fn init(value = 0) {
        self.value = value
    }

    fn add(delta) {
        self.value = self.value + delta
        return self.value
    }
}

let counter = Counter(10)
print(counter.add(5))
```

## Where To Go Next

- learn the command surface: [[CLI]]
- learn the language in detail: [[../reference/Language Reference]]
- learn the standard library: [[../reference/Standard Library Reference]]
- embed the VM in C++: [[../integrations/C++ Embedding]]
- integrate the Unity package: [[../integrations/Unity Integration]]
