fn total(v: &[i64]) -> i64 {
    return v[0usize] + v[1usize];
}
fn logos_main() -> i32 {
    let mut v: Vec<i64> = Vec::new();
    v.push(3i64);
    v.push(4i64);
    v[0usize] = total(&v);
    return (v[0usize] - 7i64) as i32;
}

fn main() { std::process::exit(logos_main()); }
