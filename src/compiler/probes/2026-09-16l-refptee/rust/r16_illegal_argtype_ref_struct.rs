struct W { p: i64 }
fn read(p: &W) -> i64 { p.p }
fn main() { let v: i64 = 7; let r: &i64 = &v; let _ = read(r); }
