#[derive(Clone, Copy)]
struct N { v: i64 }
fn main() {
    let a: N = N { v: 4i64 };
    let b: N = N { v: 4i64 };
    let ra: &N = &a;
    let rb: &N = &b;
    if ra == rb {
        std::process::exit(0);
    }
    std::process::exit(1);
}
