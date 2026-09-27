fn cond(x: i32) -> bool { return x > 3; }
fn main() {
    let msg = if cond(5) { format!("{} long", 1) } else { String::from("s") };
    println!("{}", msg);
}
