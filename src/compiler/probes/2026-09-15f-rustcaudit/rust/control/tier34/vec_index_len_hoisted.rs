fn logos_main() -> i32 {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64);
    v.push(2i64);
    let i = (v.len() - 1usize) as usize;
    v[i] = 5i64;
    return (v[1usize] - 5i64) as i32;
}
fn main() { std::process::exit(logos_main()); }
