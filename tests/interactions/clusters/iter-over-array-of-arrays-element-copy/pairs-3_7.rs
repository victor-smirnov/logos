fn bump(r: &mut [i64; 2]) { r[0] += 5; }
fn main() {
    let mut v: Vec<[i64; 2]> = vec![[1, 2], [3, 4]];
    for r in v.iter_mut() { r[1] = 0; }
    println!("{:?}", v);
    for r in v.iter_mut() { bump(r); }
    println!("{:?}", v);
    let mut w: Vec<(i64, i64)> = vec![(1, 2)];
    for t in w.iter_mut() { t.0 += 100; }
    println!("{:?}", w);
}
