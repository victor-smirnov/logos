const SIZE: usize = 3;
struct B<const N: usize> { a: [i64; N] }
fn main() { let b: B<SIZE> = B::<SIZE> { a: [1, 2, 3] }; let arr: [i64; SIZE] = [4, 5, 6]; println!("{} {}", b.a[2], arr[1]); }
