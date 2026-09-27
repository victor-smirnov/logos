use std::fmt::Display;
fn wrap<T: Display + Clone>(t: T) -> impl Display { format!("[{}]", t.clone()) }
fn main() { println!("{}", wrap(wrap(42i64))); }
