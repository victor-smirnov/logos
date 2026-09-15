struct Bar { k: i64 }
impl Bar { fn bar(self: &mut Bar, f: impl Fn()) { } }
struct Foo { thing: Bar, number: u64 }
impl Foo {
    fn foo(self: &mut Foo) {
        self.thing.bar(|| { let _ = &self.number; });
    }
}
fn main() {}
