struct P { x: i64, y: i64 }
fn main() {
    match P { x: 3, y: 4 } { P { x, y } => { std::process::exit((x + y) as i32); } }
}
