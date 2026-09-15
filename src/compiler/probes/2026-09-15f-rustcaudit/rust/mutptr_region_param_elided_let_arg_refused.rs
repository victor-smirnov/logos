fn raw<'a>(q: *mut &'a i64) -> i64 {
    return unsafe { **q };
}
fn logos_main() -> i32 {
    let v: i64 = 3i64;
    let mut r: &i64 = &v;
    let q: *mut &i64 = &mut r;
    return (raw(q) - 3i64) as i32;
}

fn main() { std::process::exit(logos_main()); }
