fn g(x: i64) -> String { if x > 3 { format!("<{}>", x) } else { format!("[{}]", x) } }
fn main() {
    let f = |x: i64| if x > 3 { format!("<{}>", x) } else { format!("[{}]", x) };
    let s = f(4); let u = f(1);
    println!("{} {} {}", s, u, g(9));
}
