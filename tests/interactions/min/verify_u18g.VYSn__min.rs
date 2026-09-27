fn main() {
    let o: Option<i32> = Some(5);
    std::process::exit(match &o { None => 0, Some(s) => *s })
}
