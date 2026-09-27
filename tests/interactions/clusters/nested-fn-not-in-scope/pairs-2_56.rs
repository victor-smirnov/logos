fn main() {
    fn first(xs: &[i64]) -> &i64 { &xs[0] }
    let a = [4i64, 5];
    println!("{}", first(&a));
}
