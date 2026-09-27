trait Dup: Clone { fn two(&self) -> (Self, Self) where Self: Sized { (self.clone(), self.clone()) } }
#[derive(Clone)]
struct T { n: i32 }
impl Dup for T {}
fn main() { let t = T { n: 3 }; let (a, b) = t.two(); println!("{} {}", a.n, b.n); }
