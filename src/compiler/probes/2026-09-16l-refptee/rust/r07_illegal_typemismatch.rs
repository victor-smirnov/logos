fn read(p: &*const i64) -> i64 { unsafe { **p } }
fn main() { let v: i64 = 7; let r: &i64 = &v; let _ = read(r); }
