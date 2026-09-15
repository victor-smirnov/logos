fn stash<'a>(v: &mut Vec<&'a i64>, x: &'a i64) {
    v.push(x);
}
fn main() {
    let mut buffer: Vec<&i64> = Vec::new();
    {
        let d: i64 = 1i64;
        stash(&mut buffer, &d);
    }
    std::process::exit(buffer.len() as i32);
}
