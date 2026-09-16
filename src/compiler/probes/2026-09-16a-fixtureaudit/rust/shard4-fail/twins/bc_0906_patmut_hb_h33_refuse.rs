#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct W { b: Box<i64> }
fn lmain() -> i32 {
    let o: Option<W> = Some(W { b: Box::new(1i64) });
    if let Some(W { b: bb }) = o {
        let r: &mut i64 = &mut *bb;
        *r = 5i64;
    }
    return 0i32;
}
fn main() {}
