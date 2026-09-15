fn get(pp: &&Option<i64>) -> i64 {
    match pp {
        &&Option::Some(x) => x,
        other => { if other.is_some() { 66i64 } else { 77i64 } }
    }
}
fn logos_main() -> i32 {
    let o: Option<i64> = Option::Some(5i64);
    let r: &Option<i64> = &o;
    if get(&r) != 5i64 { return 1i32; }
    let n: Option<i64> = Option::None;
    let rn: &Option<i64> = &n;
    if get(&rn) != 77i64 { return 2i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
