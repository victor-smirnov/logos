fn apply<F: Fn(&mut u32)>(x: &mut u32, f: F) { f(x); }
fn apply2<T, F: Fn(&mut T)>(x: &mut T, f: F) { f(x); }
fn main() {
    let mut k = 3u32;
    apply(&mut k, |v| { *v = *v * *v; });
    apply2(&mut k, |v: &mut u32| { *v = *v + 1; });
    apply2(&mut k, |v| { *v = *v + 1; });
    std::process::exit(k as i32);
}
