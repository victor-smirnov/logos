fn cnt<T>(xs: &[T]) -> i64 { xs.len() as i64 }
fn main() {
    let v: Vec<i32> = vec![1, 2, 3];
    std::process::exit(cnt(&v) as i32);
}
