struct N { id: i64 }
impl Drop for N { fn drop(&mut self) { println!("drop {}", self.id); } }
struct C { a: N, b: N }
fn mk(x: i64) -> C { C { a: N { id: x }, b: N { id: x + 1 } } }
fn f() -> Option<C> { Some(C { a: N { id: 100 }, ..mk(1) }) }
fn g() -> C { C { a: N { id: 200 }, ..mk(5) } }
fn main() {
    let o = f();
    println!("built");
    let q = g();
    println!("built2 {}", q.a.id);
    if let Some(c) = &o { println!("{} {}", c.a.id, c.b.id); }
}
