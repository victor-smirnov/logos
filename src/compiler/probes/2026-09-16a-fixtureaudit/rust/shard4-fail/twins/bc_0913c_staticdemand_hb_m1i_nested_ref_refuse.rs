#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
static FOO: u8 = 1u8;
static PF: &u8 = &FOO;
static mut PP: &&u8 = &PF;
fn set() {
    let n: u8 = 2u8;
    let r: &u8 = &n;
    unsafe { PP = &r; }
}
fn main() { set(); }
