fn h(c: bool) -> Option<i64> { let r = loop { if c { break None; } break Some(7); }; return r; }
fn m(c: bool) -> Option<i64> { let r: Option<i64> = loop { if c { break None; } break Some(7); }; return r; }
fn main() { println!("{:?} {:?} {:?} {:?}", h(true), h(false), m(true), m(false)); }
