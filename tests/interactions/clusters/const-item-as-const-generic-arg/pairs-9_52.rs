const CAP: usize = 3;
struct R<const N: usize> { b: [i64; N] }
fn main() { let r: R<CAP> = R { b: [7; CAP] }; println!("{}", r.b[2]); }
