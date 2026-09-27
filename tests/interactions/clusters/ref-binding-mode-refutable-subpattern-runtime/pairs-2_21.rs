fn d(r: &Result<(i64, i64), i64>) -> i64 {
    match r { Ok((0, y)) | Ok((y, 0)) => 1000 + *y, Ok((x, y)) => *x * 10 + *y, Err(e) => -*e }
}
fn main() { let a: Result<(i64, i64), i64> = Ok((12i64, 0i64)); let c: Result<(i64, i64), i64> = Ok((0i64, 7i64)); println!("{} {}", d(&a), d(&c)); }
