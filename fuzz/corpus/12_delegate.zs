var ev = fn() {}
ev += fn() { print("fired") }
ev()
