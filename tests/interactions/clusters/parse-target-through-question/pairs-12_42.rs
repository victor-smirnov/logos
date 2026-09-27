use std::num::ParseIntError;
fn p(x: &str) -> Result<i64, ParseIntError> { let n: i64 = x.parse()?; return Ok(n + 1); }
fn q(x: &str) -> Result<i64, ParseIntError> { let n = x.parse::<i64>()?; return Ok(n + 1); }
fn fe(v: &Vec<i64>) -> Option<i64> { let f = v.iter().find(|x| **x % 2 == 0)?; return Some(*f + 1); }
fn takes(s: &i64) -> i64 { return *s; }
fn main() {
    println!("{:?} {:?}", q("41"), p("41"));
    let v: Vec<i64> = vec![1, 4];
    println!("{:?}", fe(&v));
    let x = 5i64; let r = &x; let rr = &r;
    println!("{}", takes(rr));
}
