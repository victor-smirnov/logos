const CAP: i64 = 3;
struct R<const N: i64> { x: i64 }
fn main() { let r: R<CAP> = R { x: 7 }; println!("{}", r.x); }
