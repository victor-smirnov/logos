struct LowErr { code: i64 }
enum HighErr { Low(i64) }
impl From<LowErr> for HighErr { fn from(e: LowErr) -> HighErr { HighErr::Low(e.code) } }
fn low(v: i64) -> Result<i64, LowErr> { if v % 2 == 0 { Ok(v / 2) } else { Err(LowErr { code: v }) } }
fn lift<E: From<LowErr>>(v: i64) -> Result<i64, E> { let a = low(v)?; Ok(a) }
fn main() {
    let r: Result<i64, HighErr> = lift(3);
    match r { Ok(v) => println!("ok {}", v), Err(HighErr::Low(c)) => println!("low {}", c) }
}
