fn main() {
    let sv: ((i64, i64), i64) = ((3, 2), 2);
    match sv {
        ((a, b), z) => { if a + b + z != 7 { std::process::exit(1); } }
    }
}
