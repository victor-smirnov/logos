#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct S { v: i64 }
impl S { fn get(&mut self) -> &mut i64 { return &mut self.v; } }
fn lmain() -> i32 {
    let arr: [S; 2] = [S { v: 1i64 }, S { v: 2i64 }];
    for s in arr {
        let r: &mut i64 = s.get();
        *r = *r + 10i64;
    }
    return 0i32;
}
fn main() {}
