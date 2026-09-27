fn map_all<U, F: Fn(&i32) -> U>(v: &Vec<i32>, f: F) -> Vec<U> { let r: Vec<U> = v.iter().map(f).collect(); return r; }
fn main() { let v = vec![1, 2]; let w = map_all(&v, |x| x * 3); println!("{:?}", w); }
