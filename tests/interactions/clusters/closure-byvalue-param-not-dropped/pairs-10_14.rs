struct R { id: i32 }
impl Drop for R { fn drop(&mut self) { println!("R{}", self.id); } }
fn main() { let q = Some(R { id: 50 }); let q2 = q.map(|r| r.id + 1); println!("mapped {:?}", q2); }
