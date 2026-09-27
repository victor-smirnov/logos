struct S { v: i64 }
fn main() { let mut s = S { v: 1 }; let mut p: *mut S = &mut s; let mut f = move || { unsafe { (*p).v = 9; } }; f(); println!("{}", s.v); }
