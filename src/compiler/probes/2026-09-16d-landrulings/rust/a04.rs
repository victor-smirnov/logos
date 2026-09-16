fn main() {
    let a: [i32; 3] = [1, 2, 3];
    let r: &[i32; 3] = &a;
    std::process::exit(r[1]);
}
