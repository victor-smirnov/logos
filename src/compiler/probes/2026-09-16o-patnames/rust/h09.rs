struct P { x: i64 }
fn main() {
    let p = P { x: 5 };
    let r: &P = &p;
    match r {
        P { x } => { if *x != 5 { std::process::exit(1); } }
    }
}
