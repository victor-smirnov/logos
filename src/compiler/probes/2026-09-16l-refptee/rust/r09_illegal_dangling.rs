fn keep<'a>(x: &'a *mut i64) -> &'a *mut i64 { x }
fn main() { let r; { let mut v: i64 = 3; let p: *mut i64 = &mut v; r = keep(&p); } let _ = r; }
