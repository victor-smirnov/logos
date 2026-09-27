fn f() -> String { let s = String::from("abc"); s }
fn main() { let v = f(); println!("{}", v.len()); }
