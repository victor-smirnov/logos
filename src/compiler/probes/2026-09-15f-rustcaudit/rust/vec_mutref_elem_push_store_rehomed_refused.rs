fn logos_main() -> i32 {
    let mut x: i64 = 1i64;
    let mut y: i64 = 2i64;
    {
        let mut v: Vec<&mut i64> = Vec::new();
        v.push(&mut x);
        v.push(&mut y);
        *v[0usize] = 7i64;
        *v[1usize] = 8i64;
    }
    x = x + 1i64;
    return (x + y) as i32 - 16i32;
}

fn main() { std::process::exit(logos_main()); }
