struct H<'a> { p: &'a i64 }
fn set<'a,'b>(o: &mut H<'a>, s: &'b i64) where 'a: 'b { o.p = s; }
fn main() { std::process::exit(0); }
