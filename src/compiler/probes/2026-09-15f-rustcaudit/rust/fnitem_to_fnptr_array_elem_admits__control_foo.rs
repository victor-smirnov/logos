struct S { n: i64 }
static GS: S = S { n: 0i64 };
fn foo<'a>(x: &'a S) -> &'static S { return &GS; }
fn baz<'a>(x: &'a S) -> &'a S { return x; }
fn main() {
  let a: [fn(&S) -> &'static S; 1] = [foo];
}
