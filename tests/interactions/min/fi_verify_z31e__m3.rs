fn main() { let h = loop { if false { break None; } break Some(8); }; let k: Option<i32> = h; println!("{:?}", k); }
