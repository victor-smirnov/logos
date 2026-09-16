#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct Flag<'a> { name: &'a str, desc: &'a str }
fn mk<'a>(s: &'a str) -> Flag<'static> {
    return Flag { name: "lit", desc: s };
}
fn main() {}
