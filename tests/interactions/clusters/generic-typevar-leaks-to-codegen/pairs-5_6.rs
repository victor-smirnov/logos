

fn f<T: Copy>(o: Option<T>, d: T) -> T { let r = loop { match o { Some(x) => break x, None => break d } }; return r; }
fn main() { println!("{}", f(Some(5i64), 1)); }
