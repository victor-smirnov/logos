#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
static FOO: u8 = 4u8;
static mut TP: ((&u8, i64), i64) = ((&FOO, 1i64), 2i64);
fn set() {
    let n: u8 = 1u8;
    unsafe { TP = ((&n, 1i64), 2i64); }
}
fn main() { set(); }
