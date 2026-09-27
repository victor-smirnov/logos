trait Tr<R = Self> { type Out; }
#[allow(dead_code)] struct V { x: i64 }
impl Tr<i64> for V { type Out = V; }
impl Tr for V { type Out = i64; }
fn main() { }
