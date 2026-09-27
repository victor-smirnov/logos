fn main() {
    let mut r = 10..;
    let v = r.next();
    std::process::exit(match v { Some(x) => x, None => 99 })
}
