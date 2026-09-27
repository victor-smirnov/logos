fn main() {
    let g = |s: String, b: bool| -> i32 { if b { let _t = s; } 7 };
    std::process::exit(g(String::from("abc"), true));
}
