#![allow(static_mut_refs)]
static FOO: u8 = 4u8;
static mut BAR: &u8 = &FOO;
fn set() {
    let n: u8 = 1u8;
    unsafe {
        let p = &mut BAR;
        *p = &n;
    }
}
fn main() { set(); unsafe { std::process::exit(*BAR as i32); } }
