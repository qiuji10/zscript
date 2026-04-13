fn f(n) {
    if n > 0 {
        return f(n - 1) + f(n - 2)
    }
    return n
}
