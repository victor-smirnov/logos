struct Cat { meows: i64, how_hungry: i64 }
fn eat(c: &mut Cat) {
    c.how_hungry = c.how_hungry - 5i64;
    c.meows = c.meows + 1i64;
}
fn main() {
    let mut nyan: Cat = Cat { meows: 0i64, how_hungry: 10i64 };
    eat(&mut nyan);
    eat(&mut nyan);
    if nyan.meows != 2i64 { std::process::exit(1); }
    if nyan.how_hungry != 0i64 { std::process::exit(2); }
    std::process::exit(0);
}
