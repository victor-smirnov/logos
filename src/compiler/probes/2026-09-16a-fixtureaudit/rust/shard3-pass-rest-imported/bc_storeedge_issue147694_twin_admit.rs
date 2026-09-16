fn main() {
    let mut buffer: Vec<&i64> = Vec::new();
    let mut i: i64 = 0i64;
    let data: i64 = 2i64;
    while i < 2i64 {
        buffer.push(&data);
        i = i + 1i64;
    }
    let _ = buffer.len();
    std::process::exit(0);
}
