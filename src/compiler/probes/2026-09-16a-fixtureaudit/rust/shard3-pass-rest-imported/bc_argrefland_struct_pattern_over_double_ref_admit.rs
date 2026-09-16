struct P { x: i32 }
fn deref2(p: &&P) -> i32 {
    match p { &&P { x } => x }
}
fn main() {
    let p = P { x: 5i32 };
    let r: &P = &p;
    let pp: &&P = &r;
    if deref2(pp) != 5i32 { std::process::exit(1); }
    match pp { &&P { x } => { if x != 5i32 { std::process::exit(2); } } }
    if let &&P { x } = pp {
        if x != 5i32 { std::process::exit(3); }
    }
    match pp { &q => { if q.x != 5i32 { std::process::exit(4); } } }
    std::process::exit(0);
}
