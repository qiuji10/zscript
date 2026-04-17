# Standard Library Reference

This page documents the runtime library registered by the VM.

Unless otherwise noted, these surfaces come from `VM::open_stdlib()`. File and operating-system helpers are opt-in through `open_io()` and `open_os()`.

## Core Global Functions

### Output and Diagnostics

- `print(...)`
  Writes arguments separated by tabs, followed by a newline.
- `log(...)`
  Writes arguments without tab insertion, followed by a newline.

### Conversion and Inspection

- `tostring(value)`
- `tonumber(value)`
- `tobool(value)`
- `type(value)`
- `int(value)`
- `float(value)`
- `str(value)`

Notes:

- `tonumber` returns `nil` when conversion fails
- `int` and `float` are explicit numeric casts
- `type` returns the runtime type name string

### Error Handling

- `assert(condition, message = "assertion failed")`
- `error(message = "error")`

### Utility

- `max(...)`
- `min(...)`
- `len(value)`
- `range(stop)`
- `range(start, stop)`
- `range(start, stop, step)`

`len` works on strings and tables. For tables it returns the total key count, not just the array part.

## `math`

The `math` table is always available after `open_stdlib()`.

### Constants

- `math.pi`
- `math.huge`
- `math.inf`
- `math.nan`

### Numeric Functions

- `math.floor(x)`
- `math.ceil(x)`
- `math.round(x)`
- `math.abs(x)`
- `math.sqrt(x)`
- `math.pow(x, y)`
- `math.exp(x)`
- `math.log(x)`
- `math.log(x, base)`
- `math.log10(x)`
- `math.sin(x)`
- `math.cos(x)`
- `math.tan(x)`
- `math.asin(x)`
- `math.acos(x)`
- `math.atan(x)`
- `math.atan(y, x)`
- `math.clamp(value, min, max)`
- `math.sign(x)`
- `math.lerp(a, b, t)`
- `math.max(...)`
- `math.min(...)`
- `math.rad(degrees)`
- `math.deg(radians)`
- `math.fmod(x, y)`
- `math.is_nan(x)`
- `math.is_inf(x)`
- `math.div(x, y)`

`math.div` performs integer division and throws on division by zero.

## `string`

The `string` module is exposed as both:

- the global `string` table
- bound methods on string values, such as `"hello".upper()`

### String Functions

- `string.len(s)`
- `string.sub(s, start, end = nil)`
- `string.upper(s)`
- `string.lower(s)`
- `string.trim(s)`
- `string.trim_start(s)`
- `string.trim_end(s)`
- `string.contains(s, needle)`
- `string.starts_with(s, prefix)`
- `string.ends_with(s, suffix)`
- `string.find(s, needle)`
- `string.replace(s, from, to)`
- `string.split(s, delimiter)`
- `string.join(array, separator = "")`
- `string.rep(s, count)`
- `string.byte(s, index = 0)`
- `string.char(code)`
- `string.format(fmt, ...)`
- `string.is_empty(s)`
- `string.reverse(s)`

### Convenience Globals

The runtime also exposes:

- `split(...)`
- `join(...)`

These point to the same implementation as `string.split` and `string.join`.

## `table`

The `table` module is also bound as method-style behavior for tables and arrays.

That means both of these are valid:

```ts
table.push(arr, 10)
arr.push(10)
```

### Table Functions

- `table.len(tbl)`
- `table.push(tbl, value)`
- `table.pop(tbl)`
- `table.insert(tbl, index, value)`
- `table.remove(tbl, index = last)`
- `table.keys(tbl)`
- `table.values(tbl)`
- `table.contains(tbl, needle)`
- `table.sort(tbl)`
- `table.copy(tbl)`
- `table.slice(tbl, start, end = len)`
- `table.index_of(tbl, needle)`
- `table.reverse(tbl)`
- `table.concat(tbl_a, tbl_b, ...)`
- `table.map(tbl, fn)`
- `table.filter(tbl, fn)`
- `table.reduce(tbl, fn, initial = nil)`
- `table.each(tbl, fn)`
- `table.any(tbl, fn)`
- `table.all(tbl, fn)`
- `table.flat(tbl)`
- `table.first(tbl)`
- `table.last(tbl)`

Behavior details:

- `table.len` returns the sequential array length
- `table.sort` sorts the array part in place
- `table.copy` is shallow
- `table.map/filter/reduce/each/any/all` call back through the VM

## `json`

The runtime includes a lightweight JSON helper module:

- `json.encode(value)`
- `json.decode(string)`

Notes:

- `json.encode` skips internal hash keys starting with `__`
- tables with only an array part encode as JSON arrays
- mixed or hashed tables encode as JSON objects
- unsupported values such as functions serialize as `null`

## `coroutine`

Coroutine support is available through:

- `coroutine.create(fn)`
- `coroutine.status(co)`
- `coroutine.yield(...)`
- `coroutine.resume(co, ...)`
- `coroutine.wrap(fn)`

Behavior notes:

- `coroutine.status` returns `"suspended"`, `"running"`, or `"dead"`
- `coroutine.resume` returns success plus yielded or final values
- resuming a running or dead coroutine returns a failure result

## `io`

The `io` module is only present after `VM::open_io()`.

### IO Functions

- `io.read_file(path)`
- `io.write_file(path, contents)`
- `io.append_file(path, contents)`
- `io.lines(path)`
- `io.size(path)`
- `io.exists(path)`
- `io.delete_file(path)`
- `io.rename(from, to)`
- `io.copy_file(from, to)`
- `io.read_line()`
- `io.print_err(...)`

These APIs are intentionally opt-in because they touch the host filesystem and standard streams.

## `os`

The `os` module is only present after `VM::open_os()`.

### OS Functions

- `os.getcwd()`
- `os.chdir(path)`
- `os.mkdir(path)`
- `os.rmdir(path)`
- `os.listdir(path)`
- `os.getenv(name)`
- `os.time()`
- `os.clock()`
- `os.platform()`
- `os.exit(code = 0)`

### `os.path`

- `os.path.join(...)`
- `os.path.dirname(path)`
- `os.path.basename(path)`
- `os.path.stem(path)`
- `os.path.ext(path)`
- `os.path.abs(path)`
- `os.path.exists(path)`
- `os.path.is_file(path)`
- `os.path.is_dir(path)`

## Related Pages

- [[Language Reference]]
- [[Annotations, Tags, and Metadata]]
- [[../integrations/C++ Embedding]]
