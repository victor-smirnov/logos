struct D { v: i64 }
fn pick<'a, 'b>(a: &'b &'a D) -> &'a D {
    return *a;
}
fn logos_main() -> i32 {
    let d: D = D { v: 6i64 };
    let rd: &D = &d;
    let back: &D = pick(&rd);
    return (back.v - 6i64) as i32;
}

fn main() { std::process::exit(logos_main()); }
