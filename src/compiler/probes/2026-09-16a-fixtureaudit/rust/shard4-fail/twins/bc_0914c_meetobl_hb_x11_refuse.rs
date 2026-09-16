#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct Flag<'a> { name: &'a str, desc: &'a str }
fn set_desc<'a>(f: Flag<'a>, s: &str) -> Flag<'a> {
    return Flag { name: f.name, desc: s };
}
fn main() {}
