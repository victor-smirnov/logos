fn main() {
    let mut x: i64 = 5i64;
    let mut v: Vec<&i64> = Vec::new();
    v.push(&x);
    let w: Vec<&i64> = v;
    x = 3i64;
    std::process::exit(w.len() as i32);
}
