fn array_elem<'a, 'b>(x: &'a i64) -> *const &'b i64 {
    let z: &[&i64; 3] = &[x; 3];
    let y: *const &i64 = z as *const &i64;
    return y;
}
fn array_coerce<'a, 'b>(x: &'a i64) -> *const [&'b i64; 3] {
    let z: &[&i64; 3] = &[x; 3];
    let y: *const [&i64; 3] = z as *const [&i64; 3];
    return y;
}
fn nested_array<'a, 'b>(x: &'a i64) -> *const [&'b i64; 2] {
    let z: &[[&i64; 2]; 3] = &[[x; 2]; 3];
    let y: *const [&i64; 2] = z as *const [&i64; 2];
    return y;
}
fn main() {}
