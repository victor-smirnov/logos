fn main() {
    let sv: (Option<i64>, i64) = (Some(3), 2);
    match sv {
        (Some(a), z) => { if a + z != 5 { std::process::exit(1); } }
        (None, _) => { std::process::exit(2); }
    }
}
