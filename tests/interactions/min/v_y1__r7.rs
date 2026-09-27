fn cnt<T>(s: &[T]) -> usize { s.len() }
fn main() { cnt::<i32>(&[]); println!("{}", cnt::<i32>(&[])); }
