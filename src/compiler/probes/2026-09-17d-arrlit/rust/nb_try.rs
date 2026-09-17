fn get<'a>(x: &'a i64) -> Option<&'a i64> { Some(x) }
fn escape() -> Option<&'static i64> {
    let n: i64 = 9;
    let r: &i64 = get(&n)?;
    Some(r)
}
fn main() { let _o = escape(); }
