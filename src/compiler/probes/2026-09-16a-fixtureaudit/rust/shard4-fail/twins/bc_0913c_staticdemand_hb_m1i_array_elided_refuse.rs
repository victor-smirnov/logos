#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
static FOO: u8 = 1u8;
static mut ARR: [&u8; 2] = [&FOO, &FOO];
fn set() {
    let n: u8 = 2u8;
    unsafe { ARR[0] = &n; }
}
fn main() { set(); }
