fn map_opt<T, U, F: Fn(T) -> U>(o: Option<T>, f: F) -> Option<U> { match o { Some(x) => Some(f(x)), None => None } }
fn main() {
    println!("{:?}", map_opt(Some(5i64), |x| x * 10));
}
