struct Tok { s: i64 }
impl Drop for Tok { fn drop(&mut self) { println!("drop {}", self.s); } }
enum Msg { One(Tok), Zero }
fn f(m: Msg) -> i64 { let v = match m { Msg::One(t) => t.s, Msg::Zero => 0 }; v }
fn g(m: Msg) -> i64 { match m { Msg::One(t) => t.s, Msg::Zero => 0 } }
fn h() -> i64 { let m = Msg::One(Tok { s: 3 }); match m { Msg::One(t) => t.s, Msg::Zero => 0 } }
fn main() { println!("{}", f(Msg::One(Tok { s: 1 }))); println!("{}", g(Msg::One(Tok { s: 2 }))); println!("{}", h()); }
