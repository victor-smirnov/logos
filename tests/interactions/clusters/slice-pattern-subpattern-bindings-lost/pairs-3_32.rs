fn first2(xs: &[i64]) -> i64 {
    let [a, b, ..] = xs else { return -1; };
    *a + *b
}
fn main() { let v: [i64; 3] = [1, 2, 3]; println!("{} {}", first2(&v), first2(&v[2..])); }
