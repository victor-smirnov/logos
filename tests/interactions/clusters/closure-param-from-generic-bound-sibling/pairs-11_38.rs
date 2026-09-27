fn ap<A, T>(a: A, t: T, f: &dyn Fn(A, T) -> A) -> A { f(a, t) }
fn main() { println!("{}", ap(1i64, 2i64, &|a, x| a * 10 + x)); }
