fn first<T: Copy>(xs: &[T]) -> T { return xs[0]; }
fn main() {
    let v: Vec<i64> = vec![1, 25, 30];
    println!("{}", first(&v));
}
