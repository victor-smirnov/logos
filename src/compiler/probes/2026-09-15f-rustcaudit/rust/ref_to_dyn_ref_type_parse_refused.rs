trait Tr { fn get(self: &Self) -> i64; }
struct D { v: i64 }
impl Tr for D { fn get(self: &Self) -> i64 { return self.v; } }
fn inner(a: & &dyn Tr) -> i64 {
    let x: &dyn Tr = *a;
    return x.get();
}
fn logos_main() -> i32 {
    let d: D = D { v: 6i64 };
    let rd: &dyn Tr = &d;
    return (inner(&rd) - 6i64) as i32;
}

fn main() { std::process::exit(logos_main()); }
