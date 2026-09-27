fn b1() -> String { let s = String::from("hi"); s }
fn b2() -> i64 { let s = 4i64; s }
fn b3() -> Vec<i64> { let mut v: Vec<i64> = Vec::new(); v.push(1i64); v }
fn b4() -> Vec<i64> { let mut v: Vec<i64> = Vec::new(); v.push(1i64); { v } }
fn main() {
    println!("{}", b1());
    println!("{}", b2());
    let v = b3(); println!("{}", v.len());
    println!("{}", v[0]);
    let w = b4(); println!("{}", w[0]);
}
