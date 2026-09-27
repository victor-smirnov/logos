fn cnt<T>(x: &[T]) -> i64 { x.len() as i64 }
fn main() {
    let w: Vec<i32> = vec![1, 2];
    println!("{}", cnt(&w));
}
