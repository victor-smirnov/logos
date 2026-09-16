// TWIN: `impl Eq for D { fn eq }` translated to Rust's `impl PartialEq for D`.
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { return self.v == other.v; } }
fn main() {
    let a: D = D { v: 4i64 };
    let b: D = D { v: 4i64 };
    let ra: &D = &a;
    let rb: &D = &b;
    if ra == rb {
        std::process::exit(7);
    }
    std::process::exit(1);
}
