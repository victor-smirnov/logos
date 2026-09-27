fn main() {
    let s = "Hello";
    let bytes: Vec<u8> = s.bytes().take(3).collect();
    println!("{:?}", bytes);
}
