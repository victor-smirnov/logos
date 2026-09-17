struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.v); } }
fn get(r: &Option<D>) -> i64 { let &Option::Some(x) = r else { return 77; }; x.v }
fn main() { let o: Option<D> = Some(D { v: 2 }); println!("got={}", get(&o)); }
