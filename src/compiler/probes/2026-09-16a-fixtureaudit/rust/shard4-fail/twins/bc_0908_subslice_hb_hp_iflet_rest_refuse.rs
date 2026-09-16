#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P { n: i64 }
impl Drop for P { fn drop(&mut self) {} }
fn arr() -> [P; 3] { return [P { n: 1i64 }, P { n: 2i64 }, P { n: 3i64 }]; }
fn lmain() -> i32 {
    let a: [P; 3] = arr();
    if let [_, _, x @ ..] = a { }
    match a { [_, _, z] => { } }
    return 0i32;
}
fn main() {}
