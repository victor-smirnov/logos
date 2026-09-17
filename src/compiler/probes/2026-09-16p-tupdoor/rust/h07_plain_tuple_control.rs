fn main() {
    let sv: (i64, i64) = (1, 2);
    match sv {
        (a, b) => { if a + b != 3 { std::process::exit(1); } }
    }
    std::process::exit(0);
}
