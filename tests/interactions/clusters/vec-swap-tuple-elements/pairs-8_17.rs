fn main() {
    let mut kv: Vec<(u32, u64)> = vec![(3, 30), (1, 10), (2, 20)];
    kv.swap(0, 1);
    println!("{:?}", kv);
    let mut w: Vec<(i64, i64)> = vec![(3, 30), (1, 10)];
    w.swap(0, 1);
    println!("{:?}", w);
}
