trait T {
    fn a(self: &Self, x: i64) -> i64;
    fn b(self: &Self) -> i64 {
        return self.nosuch(1i64);
    }
}
fn main() {}
