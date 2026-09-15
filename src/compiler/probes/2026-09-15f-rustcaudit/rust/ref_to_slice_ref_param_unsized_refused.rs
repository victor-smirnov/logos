fn inner(a: &&[i64]) -> i64 {
    let x: &[i64] = *a;
    return x[1];
}
fn logos_main() -> i32 {
    let arr: [i64; 3] = [5i64, 6i64, 7i64];
    let s: &[i64] = &arr;
    return (inner(&s) - 6i64) as i32;
}

fn main() { std::process::exit(logos_main()); }
