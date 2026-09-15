fn main() {
    let x: i64 = 5i64;
    let mut arr: [&i64; 1] = [&x];
    {
        let d: i64 = 1i64;
        arr[0] = &d;
    }
    let v: i64 = *arr[0];
    std::process::exit(v as i32);
}
