fn main() {
    let kv: Vec<(i64, i64)> = vec![(7, 1), (8, 2)];
    for (i, (k, v)) in kv.iter().enumerate() { println!("B {} {} {}", i, k, v); }
    for (i, &(k, v)) in kv.iter().enumerate() { println!("C {} {} {}", i, k, v); }
    let e = (0i64, &kv[1]);
    let (a, (b, c)) = e;
    println!("D {} {} {}", a, b, c);
}
