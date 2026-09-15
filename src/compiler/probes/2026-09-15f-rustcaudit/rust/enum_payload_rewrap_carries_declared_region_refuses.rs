enum E<'s> { A(&'s i64), B }
fn keep<'a>(e: E<'a>) -> E<'a> {
    match e {
        E::A(r) => { return E::A(r); }
        E::B => { return E::B; }
    }
}
fn logos_main() -> i32 {
    let v: i64 = 35i64;
    match keep(E::A(&v)) {
        E::A(r) => { return *r as i32; }
        E::B => { return 1i32; }
    }
}

fn main() { std::process::exit(logos_main()); }
