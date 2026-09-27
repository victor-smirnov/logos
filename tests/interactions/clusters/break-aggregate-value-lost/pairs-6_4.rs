struct V { x: i32, y: i32 }
fn mk(k: i32) -> V { V { x: k, y: k } }
fn main() { let n = loop { break mk(10); }; println!("{}", n.y); }
