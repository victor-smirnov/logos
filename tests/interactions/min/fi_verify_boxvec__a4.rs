struct Pool<T> { items: Vec<Vec<T>> }
fn mk<T>() -> Pool<T> { Pool { items: Vec::new() } }
fn m() -> i32 { let p: Pool<u8> = mk(); p.items.len() as i32 + 1 }
fn main(){ std::process::exit(m()) }
