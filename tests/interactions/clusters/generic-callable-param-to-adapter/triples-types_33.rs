fn apply_all<T: Copy, U, F: Fn(T) -> U>(xs: &[T], f: &F) -> Vec<U> { let mut out = Vec::new(); for x in xs { out.push(f(*x)); } return out; }
fn main() {
    let k = 100i64;
    let add = move |x: i64| x + k;
    let v = apply_all(&[1i64, 2, 3], &add);
    println!("{:?}", v);
}
