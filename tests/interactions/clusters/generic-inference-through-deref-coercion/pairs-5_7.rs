


fn cnt<T>(xs: &[T]) -> usize { return xs.len(); }
fn first<'a, T>(xs: &'a [T]) -> &'a T { return &xs[0]; }
fn main() {
    let v: Vec<i64> = vec![4, 5, 6];
    println!("{}", cnt(&v));
    println!("{}", first(&v));
}
