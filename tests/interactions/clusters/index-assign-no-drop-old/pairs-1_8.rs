struct N { id: i64 }
impl Drop for N { fn drop(&mut self) { println!("drop {}", self.id); } }
fn main() { let mut v: Vec<N> = Vec::new(); v.push(N { id: 1 }); v[0] = N { id: 2 }; println!("end"); }
