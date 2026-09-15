struct D { v: i64 }
impl Eq for D {}
impl PartialEq for D {
    fn eq(&self, other: &D) -> bool {
        return self.v == other.v;
    }
}
fn same2<T: Eq>(a: T, b: T) -> bool {
    return a == b;
}
fn logos_main() -> i32 {
    let a: D = D { v: 4i64 };
    let b: D = D { v: 4i64 };
    if same2::<&D>(&a, &b) {
        return 0i32;
    }
    return 1i32;
}

fn main() { std::process::exit(logos_main()); }
