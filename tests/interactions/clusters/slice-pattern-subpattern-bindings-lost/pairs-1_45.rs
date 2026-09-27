fn first(t: &[(i64, i64)]) -> i64 { match t { [(k, v), ..] => *k + *v, [] => -1 } }
fn main() { let a = [(1i64, 2i64), (3, 4)]; println!("{} {}", first(&a), first(&a[..0])); }
