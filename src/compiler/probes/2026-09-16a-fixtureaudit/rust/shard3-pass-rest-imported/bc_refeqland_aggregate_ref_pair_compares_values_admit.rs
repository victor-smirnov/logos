// TWIN: `impl Eq for S { fn eq }` translated to Rust's `impl PartialEq for S`.
struct S { v: i64 }
impl PartialEq for S {
    fn eq(&self, other: &S) -> bool {
        return self.v == other.v;
    }
}
fn main() {
    let _ = S { v: 0 } == S { v: 0 };
    let t1: (i64, i64) = (1i64, 2i64);
    let t2: (i64, i64) = (1i64, 2i64);
    let rt1: &(i64, i64) = &t1;
    let rt2: &(i64, i64) = &t2;
    if !(rt1 == rt2) { std::process::exit(1); }
    let a1: [i64; 2] = [3i64, 4i64];
    let a2: [i64; 2] = [3i64, 4i64];
    let ra1: &[i64; 2] = &a1;
    let ra2: &[i64; 2] = &a2;
    if !(ra1 == ra2) { std::process::exit(2); }
    let s2: String = String::from("ab");
    let sa = "ab";
    let sb = s2.as_str();
    let x = &sa;
    let y = &sb;
    if !(x == y) { std::process::exit(3); }
    std::process::exit(0);
}
