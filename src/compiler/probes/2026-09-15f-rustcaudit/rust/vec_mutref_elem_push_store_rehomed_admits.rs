fn main() {
    let mut x: i64 = 5i64;
    let mut v: Vec<&mut i64> = Vec::new();
    v.push(&mut x);
    {
        let mut d: i64 = 1i64;
        v.push(&mut d);
    }
    std::process::exit(v.len() as i32);
}
