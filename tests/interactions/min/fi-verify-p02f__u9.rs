fn ap<T, U, F: Fn(T) -> U>(x: T, f: F) -> Option<U> { Some(f(x)) }
fn main() { let r = ap(5i64, |x| x); match r { Some(v) => println!("{}", v), None => println!("none") } }
