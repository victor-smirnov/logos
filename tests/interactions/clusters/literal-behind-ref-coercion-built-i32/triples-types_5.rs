fn show(a: &i64) -> i64 { *a }
fn main() { let x = 5; let y = 3; println!("{} {}", show(&x), show(&y)); }
