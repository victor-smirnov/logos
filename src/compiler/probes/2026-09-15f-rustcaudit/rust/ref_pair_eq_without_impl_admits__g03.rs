enum E { A, B }
fn main() {
    let a: (E, i64) = (E::A, 4i64);
    let b: (E, i64) = (E::B, 4i64);
    let ra: &(E, i64) = &a;
    let rb: &(E, i64) = &b;
    if ra == rb {
        std::process::exit(0);
    }
    std::process::exit(1);
}
