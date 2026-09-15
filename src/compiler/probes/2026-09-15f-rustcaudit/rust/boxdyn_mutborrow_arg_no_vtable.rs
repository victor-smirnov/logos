trait Sp { fn v(&self) -> i64; }
struct A { x: i64 }
impl Sp for A { fn v(&self) -> i64 { return self.x; } }
fn use_mut(r: &mut dyn Sp) -> i64 { return r.v(); }
fn logos_main() -> i32 {
    let mut b: Box<dyn Sp> = Box::new(A { x: 42i64 });
    if use_mut(&mut b) != 42i64 { return 3i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
