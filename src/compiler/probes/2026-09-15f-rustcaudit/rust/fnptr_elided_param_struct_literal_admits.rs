struct H { f: fn(&i64) -> i64 }
fn mk<'r>(g: fn(&'r i64) -> i64) -> H {
    return H { f: g };
}
fn main() {}
