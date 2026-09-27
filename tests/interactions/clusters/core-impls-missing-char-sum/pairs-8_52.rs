


fn mx<T: Ord + Copy>(a: T, b: T) -> T { if a > b { a } else { b } }
fn main() { println!("{}", mx('a', 'z')); }
