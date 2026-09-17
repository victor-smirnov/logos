fn main() {
    let arr: [i64; 2] = [3, 4];
    let sv: ([i64; 2], i64) = (arr, 5);
    let got;
    match sv {
        ([a, b], c) => { got = a + b + c; }
    }
    println!("got={}", got);
    if got != 12 { std::process::exit(1); }
    std::process::exit(0);
}
