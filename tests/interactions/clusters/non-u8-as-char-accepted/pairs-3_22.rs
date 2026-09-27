enum Val { Code(u32), No }
fn main() { let v = Val::Code(65); if let Val::Code(c) = v { let ch = c as char; println!("{}", ch); } }
