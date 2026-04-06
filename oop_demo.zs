// ============================================================
//  ZScript OOP Demo
// ============================================================

// ── 1. Basic class with init + methods ───────────────────────
class Vec2 {
    fn init(x, y) {
        self.x = x
        self.y = y
    }
    fn length() {
        return math.sqrt(self.x * self.x + self.y * self.y)
    }
    fn add(other) {
        return Vec2(self.x + other.x, self.y + other.y)
    }
    fn scale(s) {
        return Vec2(self.x * s, self.y * s)
    }
    fn dot(other) {
        return self.x * other.x + self.y * other.y
    }
    fn str() {
        return "({self.x}, {self.y})"
    }
}

var a = Vec2(3.0, 4.0)
var b = Vec2(1.0, 2.0)
var c = a.add(b)
var d = a.scale(2.0)

log("── Vec2 ───────────────────────")
log("a        = {a.str()}  len={a.length()}")
log("b        = {b.str()}")
log("a + b    = {c.str()}")
log("a * 2    = {d.str()}")
log("a dot b  = {a.dot(b)}")
log("")

// ── 2. Multiple instances, shared class, independent state ────
class Counter {
    fn init(start, label) {
        self.value = start
        self.label = label
    }
    fn inc() {
        self.value = self.value + 1
    }
    fn add(n) {
        self.value = self.value + n
    }
    fn reset() {
        self.value = 0
    }
    fn str() {
        return "{self.label}: {self.value}"
    }
}

var c1 = Counter(0, "hits")
var c2 = Counter(100, "score")

c1.inc()  c1.inc()  c1.inc()
c2.add(50)
c1.inc()
c2.add(25)

log("── Counters ────────────────────")
log(c1.str())
log(c2.str())
c1.reset()
log("after reset: {c1.str()}")
log("")

// ── 3. Composition — objects holding other objects ────────────
class Player {
    fn init(name, x, y) {
        self.name     = name
        self.pos      = Vec2(x, y)
        self.velocity = Vec2(0.0, 0.0)
        self.hp       = 100
        self.score    = 0
    }
    fn move(dx, dy) {
        self.pos = self.pos.add(Vec2(dx, dy))
    }
    fn damage(amount) {
        self.hp = self.hp - amount
        if self.hp < 0 { self.hp = 0 }
    }
    fn heal(amount) {
        self.hp = self.hp + amount
        if self.hp > 100 { self.hp = 100 }
    }
    fn collect(points) {
        self.score = self.score + points
    }
    fn is_alive() {
        return self.hp > 0
    }
    fn status() {
        log("  {self.name} | pos={self.pos.str()} | hp={self.hp} | score={self.score}")
    }
}

var player = Player("Hero", 0.0, 0.0)

log("── Player sim ──────────────────")
player.status()
player.move(3.0, 4.0)
player.collect(10)
player.status()
player.damage(30)
player.collect(25)
player.move(1.0, 0.0)
player.status()
player.damage(90)
player.status()
log("  alive? {player.is_alive()}")
player.heal(50)
log("  after heal: hp={player.hp}")
log("")

// ── 4. Class as a data structure — stack ─────────────────────
class Stack {
    fn init() {
        self.data = {}
        self.size = 0
    }
    fn push(val) {
        self.data[self.size] = val
        self.size = self.size + 1
    }
    fn pop() {
        if self.size == 0 { return nil }
        self.size = self.size - 1
        return self.data[self.size]
    }
    fn peek() {
        if self.size == 0 { return nil }
        return self.data[self.size - 1]
    }
    fn empty() {
        return self.size == 0
    }
}

var stack = Stack()
stack.push(10)
stack.push(20)
stack.push(30)

log("── Stack ───────────────────────")
log("  peek = {stack.peek()}")
log("  pop  = {stack.pop()}")
log("  pop  = {stack.pop()}")
log("  size = {stack.size}")
stack.push(99)
log("  peek = {stack.peek()}")
log("")

// ── 5. Objects interacting with each other ────────────────────
class AABB {
    fn init(x, y, w, h) {
        self.x = x
        self.y = y
        self.w = w
        self.h = h
    }
    fn overlaps(other) {
        var ok = self.x < other.x + other.w and other.x < self.x + self.w and self.y < other.y + other.h and other.y < self.y + self.h
        return ok
    }
    fn str() {
        return "AABB({self.x},{self.y} {self.w}x{self.h})"
    }
}

var box1 = AABB(0.0, 0.0, 10.0, 10.0)
var box2 = AABB(5.0, 5.0, 10.0, 10.0)
var box3 = AABB(20.0, 20.0, 5.0, 5.0)

log("── Collision ───────────────────")
log("  {box1.str()}")
log("  {box2.str()}")
log("  {box3.str()}")
log("  box1 vs box2 overlaps? {box1.overlaps(box2)}")
log("  box1 vs box3 overlaps? {box1.overlaps(box3)}")
log("")

log("done.")
