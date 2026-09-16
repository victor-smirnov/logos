fn main() {
    let x: i64 = 5i64;
    let mut buffer: Vec<&i64> = Vec::new();
    buffer.push(&x);
    {
        let data: i64 = 1i64;
        buffer.push(&data);
    }
    let y: i64 = x;
    std::process::exit((y as i32) - 5i32);
}
