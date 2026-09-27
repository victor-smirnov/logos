fn bump(v: &mut Vec<i64>) -> impl Fn(i64) -> i64 { let n = v.len() as i64; move |y| y + n }
fn main() {
    let mut v = vec![1i64, 2];
    let f = bump(&mut v);
    v.push(9);
    println!("{} {}", v.len(), f(10));
}
