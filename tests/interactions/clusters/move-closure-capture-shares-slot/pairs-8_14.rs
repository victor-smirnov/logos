


fn scaled(v: Vec<i32>, k: i32) -> impl Iterator<Item = i32> { v.into_iter().map(move |x| x * k) }
fn scaled_r(v: Vec<i32>, k: i32) -> impl Iterator<Item = i32> { return v.into_iter().map(move |x| x * k); }
fn main() {
    let w: Vec<i32> = scaled(vec![1, 2, 3], 3).collect();
    println!("{:?}", w);
    let w2: Vec<i32> = scaled_r(vec![1, 2, 3], 3).collect();
    println!("{:?}", w2);
}
