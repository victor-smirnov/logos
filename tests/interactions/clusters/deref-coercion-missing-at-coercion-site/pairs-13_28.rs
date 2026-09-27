fn n(s: &str) -> usize { return s.len(); }
fn k(x: &i64) -> i64 { return *x; }
fn main() {
    let arr = ["ab", "cde"];
    for s in arr.iter() { println!("{}", n(s)); }
    let v = [5i64, 6];
    let r = &v[0]; let rr = &r;
    println!("{}", k(rr));
}
