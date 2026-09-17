fn main() {
    let a: [i64; 2] = [1, 2]; let b: [i64; 2] = [1, 3];
    let sa: &[i64] = &a; let sb: &[i64] = &b;
    if sa < sb { std::process::exit(0); }
    std::process::exit(1);
}
