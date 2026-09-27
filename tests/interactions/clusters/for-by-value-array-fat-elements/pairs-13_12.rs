fn main() {
    let a: [&str; 2] = ["ab", "cd"];
    let mut n: usize = 0;
    for x in a { n += x.len(); }
    println!("{}", n);
}
