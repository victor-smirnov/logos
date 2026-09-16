#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
trait Show { fn n(&self) -> i64; }
struct K { v: i64 }
impl Show for K { fn n(&self) -> i64 { return self.v; } }
fn lmain() -> i32 {
    let x: K = K { v: 1i64 };
    let mut v: Vec<&dyn Show> = Vec::new();
    v.push(&x);
    {
        let d: K = K { v: 2i64 };
        v.push(&d);
    }
    return v.len() as i32;
}
fn main() {}
