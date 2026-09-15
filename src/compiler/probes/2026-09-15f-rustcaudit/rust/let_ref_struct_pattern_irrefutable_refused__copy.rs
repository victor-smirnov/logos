#[derive(Clone, Copy)]
struct P { x: i32 }
fn deref2(p: &&P) -> i32 {
    let &&P { x } = p;
    return x;
}
fn logos_main() -> i32 {
    let p = P { x: 5i32 };
    let r: &P = &p;
    let pp: &&P = &r;
    return deref2(pp) - 5i32;
}

fn main() { std::process::exit(logos_main()); }
