enum S { R { w: i64, h: i64 } }
fn f(s: &S) -> i64 { match s { S::R { w: 1..=100, h } => *h, S::R { w, .. } => *w } }
fn main() { println!("{} {}", f(&S::R { w: 5, h: 7 }), f(&S::R { w: 500, h: 7 })); }
