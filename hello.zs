// ============================================================
//  ZScript Demo — no game engine, just the language
// ============================================================

// ── 1. String interpolation + basic math ─────────────────────
let version = "1.0"
log("ZScript {version} demo\n")

// ── 2. Immutable vs mutable ──────────────────────────────────
let pi     = 3.14159265
var radius = 5.0
var area   = pi * radius * radius
log("circle r={radius}  area={area}\n")

// ── 3. Recursion — fibonacci ──────────────────────────────────
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}

log("fibonacci:")
for let i in 0..<12 {
    log("  fib({i}) = {fib(i)}")
}
log("")

// ── 4. First-class functions ──────────────────────────────────
fn double(x)  { return x * 2 }
fn square(x)  { return x * x }
fn negate(x)  { return 0 - x }

fn apply(f, x) { return f(x) }
fn compose(f, g, x) { return f(g(x)) }

log("first-class functions:")
log("  apply(double, 5)        = {apply(double, 5)}")
log("  apply(square, 4)        = {apply(square, 4)}")
log("  compose(double, square, 3) = {compose(double, square, 3)}")
log("  compose(negate, double, 7) = {compose(negate, double, 7)}")
log("")

// ── 5. Tables as structs ──────────────────────────────────────
fn make_vec2(x, y) {
    var v = {}
    v.x = x
    v.y = y
    return v
}

fn vec2_add(a, b)  { return make_vec2(a.x + b.x, a.y + b.y) }
fn vec2_scale(v, s){ return make_vec2(v.x * s,   v.y * s)   }
fn vec2_dot(a, b)  { return a.x * b.x + a.y * b.y           }
fn vec2_len(v)     { return math.sqrt(v.x * v.x + v.y * v.y)}
fn vec2_str(v)     { return "({v.x}, {v.y})"                 }

var a = make_vec2(3.0, 4.0)
var b = make_vec2(1.0, 2.0)

log("vectors:")
log("  a        = {vec2_str(a)}  len={vec2_len(a)}")
log("  b        = {vec2_str(b)}")
log("  a + b    = {vec2_str(vec2_add(a, b))}")
log("  a * 2    = {vec2_str(vec2_scale(a, 2.0))}")
log("  a dot b  = {vec2_dot(a, b)}")
log("")

// ── 6. Tables as arrays ───────────────────────────────────────
fn array_new() {
    var arr = {}
    arr.len = 0
    return arr
}

fn array_push(arr, val) {
    arr[arr.len] = val
    arr.len = arr.len + 1
}

fn array_sum(arr) {
    var s = 0
    var i = 0
    while i < arr.len { s = s + arr[i]  i = i + 1 }
    return s
}

fn array_max(arr) {
    var m = arr[0]
    var i = 1
    while i < arr.len {
        if arr[i] > m { m = arr[i] }
        i = i + 1
    }
    return m
}

fn array_str(arr) {
    var s = "["
    var i = 0
    while i < arr.len {
        s = s + tostring(arr[i])
        if i < arr.len - 1 { s = s + ", " }
        i = i + 1
    }
    return s + "]"
}

var nums = array_new()
array_push(nums, 2)
array_push(nums, 7)
array_push(nums, 1)
array_push(nums, 9)
array_push(nums, 4)
array_push(nums, 3)

log("array {array_str(nums)}:")
log("  sum = {array_sum(nums)}")
log("  max = {array_max(nums)}")
log("")

// ── 7. Bubble sort ────────────────────────────────────────────
fn bubble_sort(arr) {
    var n = arr.len
    var i = 0
    while i < n {
        var j = 0
        while j < n - i - 1 {
            if arr[j] > arr[j + 1] {
                var tmp    = arr[j]
                arr[j]     = arr[j + 1]
                arr[j + 1] = tmp
            }
            j = j + 1
        }
        i = i + 1
    }
}

bubble_sort(nums)
log("sorted: {array_str(nums)}\n")

// ── 8. Index-based binary tree ────────────────────────────────
fn make_node(val, left, right) {
    var n   = {}
    n.val   = val
    n.left  = left   // 0 = no child
    n.right = right
    return n
}

var tree = {}
tree[1] = make_node(10, 2, 3)
tree[2] = make_node(5,  4, 5)
tree[3] = make_node(15, 0, 0)
tree[4] = make_node(2,  0, 0)
tree[5] = make_node(7,  0, 0)

fn tree_sum(nodes, idx) {
    if idx == 0 { return 0 }
    var node = nodes[idx]
    return node.val + tree_sum(nodes, node.left) + tree_sum(nodes, node.right)
}

fn tree_depth(nodes, idx) {
    if idx == 0 { return 0 }
    var node  = nodes[idx]
    var ld    = tree_depth(nodes, node.left)
    var rd    = tree_depth(nodes, node.right)
    if ld > rd { return ld + 1 }
    return rd + 1
}

log("binary tree  (10 (5 (2) (7)) (15)):")
log("  sum   = {tree_sum(tree, 1)}")
log("  depth = {tree_depth(tree, 1)}")
log("")

// ── 9. Memoised fibonacci (table as cache) ────────────────────
var memo = {}

fn fib_memo(n) {
    if memo[n] != nil { return memo[n] }
    if n <= 1 { return n }
    var result = fib_memo(n - 1) + fib_memo(n - 2)
    memo[n] = result
    return result
}

log("memoised fib(30) = {fib_memo(30)}\n")

// ── 10. Mini state machine ────────────────────────────────────
fn make_fsm() {
    var fsm   = {}
    fsm.state = "idle"
    fsm.ticks = 0
    return fsm
}

fn fsm_tick(fsm, input) {
    fsm.ticks = fsm.ticks + 1
    if fsm.state == "idle" {
        if input == "start" { fsm.state = "running" }
    } else if fsm.state == "running" {
        if input == "pause" { fsm.state = "paused" }
        if input == "stop"  { fsm.state = "idle"   }
    } else if fsm.state == "paused" {
        if input == "resume" { fsm.state = "running" }
        if input == "stop"   { fsm.state = "idle"    }
    }
}

var fsm = make_fsm()
log("state machine:")
log("  state = {fsm.state}")
fsm_tick(fsm, "start")  log("  + start  -> {fsm.state}")
fsm_tick(fsm, "pause")  log("  + pause  -> {fsm.state}")
fsm_tick(fsm, "resume") log("  + resume -> {fsm.state}")
fsm_tick(fsm, "stop")   log("  + stop   -> {fsm.state}")
log("  ticks: {fsm.ticks}")
log("")

log("done.")
