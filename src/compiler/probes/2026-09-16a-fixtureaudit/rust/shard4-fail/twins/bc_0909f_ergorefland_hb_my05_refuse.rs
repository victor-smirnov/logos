#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum Inner { A(i64), B }
fn lmain() -> i32 {
    let mut o: Option<Inner> = Some(Inner::A(3i64));
    match &mut o {
        Some(Inner::A(ref mut v)) => { *v = *v + 1i64; }
        _ => {}
    }
    return 0i32;
}
fn main() {}
