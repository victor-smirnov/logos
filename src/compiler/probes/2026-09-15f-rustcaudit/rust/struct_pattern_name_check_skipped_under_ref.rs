struct P { x: i64 }
struct Q { x: i64 }
fn main() {
    let p: P = P { x: 3i64 };
    match &p {
        Q { x } => { if *x != 3i64 { std::process::exit(1); } }
    }
    std::process::exit(0);
}
