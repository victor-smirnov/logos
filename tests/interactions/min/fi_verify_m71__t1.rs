struct B { t: String, n: i64 }
impl B { fn name(&self) -> &str { return self.t.as_str(); } fn nr(&self) -> &i64 { return &self.n; } }
fn f(bs: &Vec<B>) -> Option<&str> { return bs.iter().map(|b| b.name()).next(); }
fn main() { let bs: Vec<B> = vec![B { t: String::from("xy"), n: 4 }]; println!("{}", f(&bs).unwrap()); }
