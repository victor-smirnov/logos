struct P { a: i64 }
fn main() { let x = P { a: 1 }; let y = P { a: 2 }; let rx: &P = &x; let ry: &P = &y;
    if rx < ry { std::process::exit(1); } let _ = x.a + y.a; }
