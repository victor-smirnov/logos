fn main() {
    let v: Vec<i64> = vec![1i64, 2i64, 3i64];
    let w: Vec<_> = v.into_iter().collect();
    std::process::exit(w.len() as i32);
}
