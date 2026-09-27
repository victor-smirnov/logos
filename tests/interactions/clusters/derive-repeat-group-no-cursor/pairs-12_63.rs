#[derive(Clone)]
struct Pt { x: i32 }
fn dup<T: Clone>(t: &T) -> T { return t.clone(); }
fn main() { let p = Pt { x: 1 }; let q = p.clone(); let r = dup(&p); println!("{} {}", q.x, r.x); }
