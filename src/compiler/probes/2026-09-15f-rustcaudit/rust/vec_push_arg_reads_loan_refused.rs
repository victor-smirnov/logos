fn logos_main() -> i32 {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64);
    v.push(2i64);
    let e: &i64 = &v[1usize];
    v.push(*e + 1i64);
    return (v[2usize] - 3i64) as i32;
}

fn main() { std::process::exit(logos_main()); }
