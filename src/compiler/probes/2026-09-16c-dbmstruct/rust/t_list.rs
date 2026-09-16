// TWIN of tests/imported/pass/structs-enums/generic-recursive-list-se.logos
// TWIN: Logos infers the reference lifetime in the enum field; Rust needs it spelled.
enum List<'a, T> { Cons { head: T, tail: &'a List<'a, T> }, Nil }
fn sum(l: &List<i64>) -> i64 {
    match l {
        List::Cons { head: h, tail: t } => { return *h + sum(t); }
        List::Nil => { return 0; }
    }
}
fn main() {
    let n: List<i64> = List::Nil;
    let c3: List<i64> = List::Cons { head: 3, tail: &n };
    let c2: List<i64> = List::Cons { head: 2, tail: &c3 };
    let c1: List<i64> = List::Cons { head: 1, tail: &c2 };
    if sum(&c1) == 6 { std::process::exit(0); }
    std::process::exit(1);
}
