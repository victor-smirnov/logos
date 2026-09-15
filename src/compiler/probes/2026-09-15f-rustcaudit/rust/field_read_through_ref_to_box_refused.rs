struct D { v: i64 }
fn logos_main() -> i32 {
    let b: Box<D> = Box::new(D { v: 6i64 });
    let rb: &Box<D> = &b;
    return (rb.v - 6i64) as i32;
}

fn main() { std::process::exit(logos_main()); }
