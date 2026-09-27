fn main() {
    let f = |s: String| -> i64 { let n = s.len() as i64; return n; };
    println!("{}", f(String::from("hello")));
    let g = |s: String| s.len();
    println!("{}", g(String::from("hey")));
}
