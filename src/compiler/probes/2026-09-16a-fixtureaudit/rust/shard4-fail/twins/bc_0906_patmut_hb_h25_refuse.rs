#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let o: Option<Box<i64>> = Some(Box::new(1i64));
    if let Some(bb) = o {
        let r: &mut i64 = &mut *bb;
        *r = 8i64;
    }
    return 0i32;
}
fn main() {}
