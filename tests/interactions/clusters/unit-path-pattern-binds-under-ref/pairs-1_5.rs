fn g(n: &Option<i64>) -> i64 { match n { Some(b) => *b, None => 0 } }
fn h(n: &Option<i64>) -> i64 { match n { None => 0, Some(b) => *b } }
fn k(n: &Option<i64>) -> i64 { match *n { None => 0, Some(b) => b } }
fn m(n: &Option<i64>) -> i64 { match n { &None => 0, &Some(b) => b } }
fn main() { let c = Some(7i64); println!("{} {} {} {} {}", g(&c), h(&c), k(&c), m(&c), { let nn: Option<i64> = None; h(&nn) }); }
