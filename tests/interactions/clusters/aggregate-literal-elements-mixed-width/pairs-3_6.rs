fn main() {
    let ts = [(1i64, 2i8), (3, 4), (5, 6)];
    println!("{} {}", ts[1].0, ts[2].1);
    let n = ts.len();
    println!("{}", n);
}
