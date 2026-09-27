fn pow<T>(b: T, _e: T) -> T { return b; }
fn main() { let x = unsafe { pow(2.0, 3.0) }; std::process::exit(x as i32); }
