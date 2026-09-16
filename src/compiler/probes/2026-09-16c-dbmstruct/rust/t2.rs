struct H<'a> { r: &'a i32 }
impl<'a> H<'a> { fn stash<'b>(&mut self, o: &'b i32) where 'a: 'b { self.r = o; } }
fn main() { let v: i32 = 1; let mut h = H { r: &v }; { let tmp: i32 = 2; h.stash(&tmp); } std::process::exit(*h.r - 1); }
