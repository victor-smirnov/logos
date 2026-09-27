fn main() {
    let k = 10i64;
    let a: Option<i64> = None;
    let b: Option<i64> = Some(4);
    let r: Result<i64, i64> = Err(3);
    println!("{} {:?} {}", a.unwrap_or_else(|| k + 1), b.and_then(|x| Some(x * k)), r.unwrap_or_else(|e| e + k));
    let e: Result<i64, i64> = a.ok_or_else(|| k);
    println!("{:?}", e);
}
