#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
static FOO: u8 = 1u8;
static mut BAR: &u8 = &FOO;
fn keep<'a>(p: &'a u8) {
    unsafe { BAR = p; }
}
fn main() { let n: u8 = 3u8; keep(&n); }
