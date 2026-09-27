fn f(t: Option<i64>, op: char) -> Result<i64, i32> {
    return match t {
        None => Ok(0),
        Some(b) => { match op { '+' => Ok(b + 1), _ => if b == 0 { Err(1) } else { Ok(b - 1) } } }
    };
}
fn main() {
    match f(Some(5), '+') { Ok(v) => println!("{}", v), Err(e) => println!("err {}", e) }
    match f(Some(0), '-') { Ok(v) => println!("{}", v), Err(e) => println!("err {}", e) }
    match f(Some(9), '-') { Ok(v) => println!("{}", v), Err(e) => println!("err {}", e) }
}
