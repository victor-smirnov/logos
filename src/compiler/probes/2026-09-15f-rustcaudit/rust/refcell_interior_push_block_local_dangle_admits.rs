use std::cell::RefCell;
struct H<'a, 'b> { c: &'a RefCell<Vec<&'b i64>> }
impl<'a, 'b> H<'a, 'b> {
    fn set(self: &mut H<'a, 'b>, c: &'a RefCell<Vec<&'b i64>>) { self.c = c; }
}
fn main() {
    let g: RefCell<Vec<&i64>> = RefCell::<Vec<&i64>>::new(Vec::<&i64>::new());
    let cell: RefCell<Vec<&i64>> = RefCell::<Vec<&i64>>::new(Vec::<&i64>::new());
    let mut h: H = H { c: &g };
    h.set(&cell);
    {
        let d: i64 = 2i64;
        let mut gd = h.c.borrow_mut();
        gd.push(&d);
    }
    let n: i64 = cell.borrow().len() as i64;
    std::process::exit(n as i32);
}
