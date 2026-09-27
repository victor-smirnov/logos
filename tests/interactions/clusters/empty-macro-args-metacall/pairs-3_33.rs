fn main() {
    let v: Vec<i64> = vec![3, 1, 2];
    let Some(m) = v.iter().max() else { panic!() };
    println!("{}", m);
}
