enum E {
    V(i64),
}
fn logos_main() -> i32 {
    let f = E::V;
    let e = f(4i64);
    return match e {
        E::V(x) => if x == 4i64 { 0i32 } else { 1i32 },
    };
}

fn main() { std::process::exit(logos_main()); }
