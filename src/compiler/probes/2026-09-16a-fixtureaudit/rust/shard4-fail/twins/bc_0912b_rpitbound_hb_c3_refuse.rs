#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct Own { v: i64 }
impl Drop for Own { fn drop(&mut self) { } }
fn eat(o: Own) -> i64 { return o.v; }
fn mk() -> impl FnMut() -> i64 {
    let o: Own = Own { v: 5i64 };
    return move || -> i64 { return eat(o); };
}
fn lmain() -> i32 {
    let mut f = mk();
    return f() as i32;
}
fn main() {}
