fn main() {
    let mut n = 0;
    let mut bump = |x| { n += x; };
    bump(3); bump(4);
    println!("{}", n);
}
