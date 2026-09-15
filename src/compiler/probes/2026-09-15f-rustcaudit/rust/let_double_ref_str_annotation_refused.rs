fn logos_main() -> i32 {
    let a: &str = "ab";
    let x: &&str = &a;
    if x.len() != 2usize {
        return 1i32;
    }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
