fn main() {
    let o: Option<i64> = None;
    println!("{}", o.unwrap_or_else(|| 5));
    let v: Vec<i64> = vec![1, 2];
    let w: Vec<i64> = v.iter().map(|x| x + 1).collect();
    let z: Vec<i64> = v.iter().map(|_x| 9).collect();
    println!("{:?} {:?}", w, z);
}
