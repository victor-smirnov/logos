fn a(lim: i64) -> i64 { let f = loop { if lim > 5 { break lim; } break -1; }; return f; }
fn b(lim: i64) -> i64 { let f = loop { if lim > 5 { break lim; } break 3; }; return f; }
fn main() {
    println!("{} {}", a(9), a(1));
    println!("{} {}", b(9), b(1));
}
