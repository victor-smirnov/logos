#[derive(PartialEq, PartialOrd)]
struct V { a: u32, b: u32 }
fn main() { let x = V { a: 1, b: 5 }; let y = V { a: 1, b: 7 }; println!("{} {} {:?}", x < y, x >= y, x.partial_cmp(&y)); }
