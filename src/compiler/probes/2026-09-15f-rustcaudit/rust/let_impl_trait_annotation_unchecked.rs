trait Speak { fn speak(&self) -> i64; }
struct A { v: i64 }
struct B { v: i64 }
impl Speak for B {
    fn speak(&self) -> i64 { return self.v; }
}
fn main() {
    let s: impl Speak = A { v: 1i64 };
    let mut x: i64 = 0i64;
    let f: impl Fn() -> i64 = move || -> i64 { x += 1i64; return x; };
    std::process::exit(f() as i32);
}
