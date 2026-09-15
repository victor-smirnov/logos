fn main() {
    let mut buffer: Vec<&i64> = Vec::new();
    let mut i: i64 = 0i64;
    while i < 2i64 {
        let data: i64 = i;
        buffer.push(&data);
        i = i + 1i64;
    }
    std::process::exit(0);
}
