trait Speak { fn speak(&self) -> i64; }
struct A { v: i64 }
struct B { v: i64 }
impl Speak for B {
    fn speak(&self) -> i64 { return self.v; }
}
fn as_speak(s: impl Speak) {}
fn as_fn(f: impl Fn() -> i64) -> i64 { return f(); }
fn main() {
    as_speak(A { v: 1i64 });
    let mut x: i64 = 0i64;
    std::process::exit(as_fn(move || -> i64 { x += 1i64; return x; }) as i32);
}
