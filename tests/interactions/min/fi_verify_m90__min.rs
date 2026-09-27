fn main() { let x = { fn f() -> i64 { 4 } f() }; println!("{}", x); }
