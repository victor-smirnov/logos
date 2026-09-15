fn main() {
    let mut x: i64 = 5i64;
    let mut v: Vec<&i64> = Vec::new();
    v.push(&x);
    let r: &i64 = v[0];
    x = 9i64;
    std::process::exit(*r as i32);
}
