#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
static FOO: u8 = 42u8;
static mut BAR: &'static u8 = &FOO;
fn set_bar() {
    let n: u8 = 42u8;
    unsafe { BAR = &n; }
}
fn main() { set_bar(); }
