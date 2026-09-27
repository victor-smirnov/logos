struct Meters(i64);
impl From<i64> for Meters { fn from(x: i64) -> Self { return Meters(x * 100); } }
fn main() {
    let m: Meters = 5.into();
    println!("{}", m.0);
}
