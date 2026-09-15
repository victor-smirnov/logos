fn f<'a, 'b>(x: & &'a i64) -> *const &'b i64 {
    x
}
fn main() {}
