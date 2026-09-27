fn main() {
    let xs = [1i64, 2, 3];
    let v = xs.iter().collect::<Vec<_>>();
    println!("{}", v.len());
}
