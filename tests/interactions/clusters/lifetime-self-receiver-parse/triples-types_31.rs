struct S { v: Vec<i64> }
impl S {
    fn first<'a>(&'a self) -> &'a i64 { &self.v[0] }
}
fn main() { let s = S { v: vec![4] }; println!("{}", s.first()); }
