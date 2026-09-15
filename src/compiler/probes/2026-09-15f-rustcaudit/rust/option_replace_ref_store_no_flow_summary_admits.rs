fn main() {
    let x: i64 = 5i64;
    let mut o: Option<&i64> = Option::None;
    o.replace(&x);
    {
        let d: i64 = 2i64;
        o.replace(&d);
    }
    if o.is_some() { std::process::exit(1); }
    std::process::exit(0);
}
