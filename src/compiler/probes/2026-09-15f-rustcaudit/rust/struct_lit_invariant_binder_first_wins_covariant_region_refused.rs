struct C<'a> { r: &'a i64, m: &'a mut &'a i64 }
fn mk<'a, 'b: 'a>(m: &'a mut &'a i64, r: &'b i64) -> C<'a> {
    return C { r: r, m: m };
}
fn logos_main() -> i32 {
    let a: i64 = 2i64;
    let b: i64 = 3i64;
    let mut ra: &i64 = &a;
    let c = mk(&mut ra, &b);
    return (*c.r + **c.m) as i32;
}

fn main() { std::process::exit(logos_main()); }
