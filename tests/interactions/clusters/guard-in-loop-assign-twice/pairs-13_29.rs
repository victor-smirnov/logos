enum PErr { Bad(i64), Eof }
fn run(x: i64) -> Result<i64, PErr> { if x > 2 { return Err(PErr::Bad(x)); } if x < 0 { return Err(PErr::Eof); } return Ok(x * 10); }
fn main() {
    for x in [1i64, 3, -1] {
        match run(x) {
            Ok(v) => println!("ok {}", v),
            Err(PErr::Bad(c)) => println!("bad {}", c),
            Err(PErr::Eof) => println!("eof"),
        }
    }
}
