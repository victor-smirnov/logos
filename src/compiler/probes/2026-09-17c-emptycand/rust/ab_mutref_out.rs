static V: i64 = 5;
fn stash<'a>(x: &'a i64, out: &mut &'a i64) { *out = x; }
fn main() { let mut r: &i64 = &V; { let n: i64 = 9; stash(&n, &mut r); } println!("{}", r); }
