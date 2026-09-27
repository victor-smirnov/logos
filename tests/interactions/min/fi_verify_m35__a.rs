

fn id(x: i64) -> i64 { x }
fn main() {
    let v: Vec<i64> = vec![1, 2, 3];
    let w: Vec<i64> = v.iter().map(|&x| id(x)).collect();
    println!("{:?}", w);
}
