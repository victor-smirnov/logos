fn main() {
    let a: [i32; 3] = [7, 8, 9];
    let q: *const [i32; 3] = &a;
    std::process::exit(unsafe { (*q)[0] });
}
