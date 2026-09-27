fn s1(it: impl Iterator<Item = i64>) -> i64 { let mut s: i64 = 0; for x in it { s += x; } return s; }
fn s2<I: Iterator<Item = i64>>(it: I) -> i64 { let mut s: i64 = 0; for x in it { s += x; } return s; }
fn main() {
    println!("{}", s1(1i64..4i64));
    println!("{}", s2(1i64..4i64));
    println!("{}", s2(1..4));
}
