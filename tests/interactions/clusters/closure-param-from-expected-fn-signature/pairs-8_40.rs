




fn each<F: FnMut(i64)>(n: i64, f: &mut F) { let mut i = 0; while i < n { f(i); i += 1; } }
fn main() {
    let mut acc: Vec<i64> = Vec::new();
    each(3, &mut |v| acc.push(v * 10));
    println!("{:?}", acc);
    let b: Box<dyn Fn(i64) -> Option<i64>> = Box::new(|x| Some(x + 1));
    println!("{:?}", b(4));
}
