fn main() {
    let x = 5i64; let y = 7i64;
    let rx = &x; let rrx = &rx;
    let mut b: &i64 = &y;
    b = if *b > 0 { rrx } else { b };
    let c: &i64 = if true { rrx } else { &y };
    println!("{} {}", b, c);
}
