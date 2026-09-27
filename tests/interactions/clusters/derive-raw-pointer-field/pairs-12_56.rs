#[derive(Hash, PartialEq, Eq, PartialOrd, Ord)]
struct H { p: *const i64, tag: u8 }
fn main() { let x = 1i64; let h = H { p: &x, tag: 1 }; println!("{}", h.tag); }
