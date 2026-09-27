fn first_last<T: Copy>(s: &[T]) -> Option<(T, T)> {
    match s { [] => None, [x] => Some((*x, *x)), [f, .., l] => Some((*f, *l)) }
}
fn main() {
    println!("{:?}", first_last::<i32>(&[]));
}
