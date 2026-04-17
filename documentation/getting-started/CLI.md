# CLI

The `zsc` executable is the primary developer-facing command-line entrypoint.

The command implementation lives in `src/main.cpp`.

## Command Forms

### Shorthand Execution

```bash
zsc script.zs
```

This is equivalent to:

```bash
zsc run script.zs
```

### Explicit Commands

- `zsc run [--tag=<name>]... <file.zs>`
- `zsc check [--tag=<name>]... <file.zs>`
- `zsc compile [--tag=<name>]... <file.zs> [-o out.zbc]`
- `zsc disasm <file.zbc>`
- `zsc dump <file.zs>`
- `zsc lsp`
- `zsc dap`

## `run`

Compiles source and immediately executes it.

```bash
zsc run game.zs
zsc run --tag=windows --tag=unity game.zs
```

Use `--tag` when your script contains tag-gated blocks such as `@windows { ... }`.

## `check`

Parses and compiles the file without running it.

```bash
zsc check gameplay.zs
```

Use this when you want fast validation in editors, CI, or pre-commit checks.

## `compile`

Compiles source into bytecode.

```bash
zsc compile gameplay.zs -o gameplay.zbc
```

The compiled output can be inspected with `disasm`.

## `disasm`

Disassembles a compiled `.zbc` file into a human-readable bytecode listing.

```bash
zsc disasm gameplay.zbc
```

This is mainly useful for compiler work, debugging, and regression inspection.

## `dump`

Dumps the parsed AST of a source file.

```bash
zsc dump gameplay.zs
```

Use this when diagnosing parser behavior or language feature lowering.

## `lsp`

Starts the language server process.

This command is intended to be launched by editor tooling such as the VS Code extension.

## `dap`

Starts the debugger adapter process.

This is the entrypoint used by debugger integrations.

## Exit Behavior

- successful parse / compile / run returns exit code `0`
- compiler or runtime failures return a non-zero exit code

## Related Pages

- [[Quick Start]]
- [[../reference/Language Reference]]
- [[../reference/Annotations, Tags, and Metadata]]
