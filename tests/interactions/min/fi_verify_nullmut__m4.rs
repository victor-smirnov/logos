struct G<T> { p: Option<T> }
fn none<T>() -> Option<T> { return None; }
fn mk2<T>() -> G<T> { return G { p: none() }; }
fn main() { let g: G<i64> = mk2(); println!("{}", g.p.is_none()); }
