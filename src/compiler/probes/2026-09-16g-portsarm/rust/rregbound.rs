// repaired port twin: upstream's own Inv<'a> carrier, trait flattened to a free fn
struct Inv<'a> { x: &'a mut &'a i64 }
fn method<'x, 'y: 'x>(_x: Inv<'x>, _y: Inv<'y>) { }
fn caller2<'a, 'b>(a: Inv<'a>, b: Inv<'b>) { method(a, b); }
fn main() { std::process::exit(0); }
