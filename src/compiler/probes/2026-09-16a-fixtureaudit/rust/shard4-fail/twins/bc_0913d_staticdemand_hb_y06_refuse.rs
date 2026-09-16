#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
static FOO: u8 = 4u8;
static mut BAR: &u8 = &FOO;
fn set() {
    let n: u8 = 1u8;
    let r: &u8 = &n;
    unsafe { BAR = r; }
}
fn main() { set(); }
