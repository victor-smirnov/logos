fn main() {
    let arr: [i64; 2] = [3, 4];
    let sv: ([i64; 2], i64) = (arr, 5);
    match sv {
        ([a, b], c) => { if a + b + c != 12 { std::process::exit(1); } }
    }
    std::process::exit(0);
}
