// TWIN of tests/imported/pass/enum/struct-like-variant-match.logos (the f() body, verbatim shape)
enum Foo { Bar { x: i64, y: i64 }, Baz { x: f64, y: f64 } }
fn f(x: &Foo) {
    match x {
        Foo::Baz { x, y } => { assert_eq!(x, 1.0f64); assert_eq!(y, 2.0f64); }
        Foo::Bar { y, x } => { assert_eq!(x, 1i64);   assert_eq!(y, 2i64); }
    }
}
fn main() { let x = Foo::Bar { x: 1i64, y: 2i64 }; f(&x); std::process::exit(0); }
