fn main() {
    let s = String::from("a\n b\n\nc");
    let w: Vec<&str> = s.as_str().split('\n').filter(|w| !w.is_empty()).collect();
    println!("{:?}", w);
}
