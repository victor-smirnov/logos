fn mk_boxed() -> Box<dyn Fn() -> i64> {
    let mut x: i64 = 0i64;
    return Box::new(move || -> i64 { x += 1i64; return x; });
}
fn call_ref(f: &dyn Fn() -> i64) -> i64 {
    return f();
}
fn main() {
    let b = mk_boxed();
    let mut y: i64 = 0i64;
    let annotated: Box<dyn Fn() -> i64> = Box::new(move || -> i64 { y += 2i64; return y; });
    let mut z: i64 = 0i64;
    let c = move || -> i64 { z += 4i64; return z; };
    std::process::exit((b() + annotated() + call_ref(&c)) as i32);
}
