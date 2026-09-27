fn main() { let x = 5i64; let r: Result<&i64, &str> = Ok(&x); println!("{:?}", r); }
