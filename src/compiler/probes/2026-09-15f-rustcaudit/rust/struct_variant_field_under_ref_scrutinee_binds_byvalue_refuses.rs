enum E<'s> { V { r: &'s i64 }, N }
fn again<'a>(e: &E<'a>) -> E<'a> {
    match e {
        E::V { r } => { return E::V { r: *r }; }
        E::N => { return E::N; }
    }
}
fn logos_main() -> i32 {
    let v: i64 = 14i64;
    let e = E::V { r: &v };
    match again(&e) {
        E::V { r } => { return *r as i32; }
        E::N => { return 1i32; }
    }
}

fn main() { std::process::exit(logos_main()); }
