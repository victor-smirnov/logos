#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
static FOO: u8 = 42u8;
static mut BAR: &'static u8 = &FOO;
fn set_bar(p: &u8) {
    unsafe { BAR = p; }
}
fn main() { let n: u8 = 1u8; set_bar(&n); }
