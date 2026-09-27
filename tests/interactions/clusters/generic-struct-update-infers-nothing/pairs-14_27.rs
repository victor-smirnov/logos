struct R<S> { s: S, tag: i64 }
fn main() { let r2 = R { tag: 2, ..R { s: 5u8, tag: 1 } }; println!("{}", r2.tag); }
