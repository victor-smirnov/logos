#!/usr/bin/env python3
"""THE CLOSURE LATTICE.

Axes, derived from the tree (the closure surface the compiler actually accepts,
probed by hand before this file was written; the payload table is the drop
lattice's, re-verified here):

  CAPTURE MODE      how       by shared reference (non-`move`) - by `move` with the
                              body only READING - by `move` with the body MOVING the
                              capture out - an RFC-2229 NARROW field capture - a `&mut`
                              to an outer local - a scalar `Copy` capture
  WHAT IS CAPTURED  pay       a bare Drop struct - a droppable FIELD - two droppable
                              fields - a tuple - a tuple of two owners - [D;2] - [D;3] -
                              Vec<D> - Box<D> - an enum payload - a nested struct -
                              G<D> - Box<dyn Tr> - Box<W1> - String - str (fat) - i64
  WHERE IT GOES     go        called in place - stored then called - called twice -
                              NEVER called - passed by value to `F: Fn` - passed as
                              `&F` / `&mut F` - Box<dyn Fn> in the same frame -
                              Box<dyn Fn> RETURNED (escaping, heap env) - in a Vec -
                              coerced to a bare fn pointer
  TRAIT SHAPE       shape     Fn - FnMut - FnOnce - fn(..) pointer - generic F -
                              dyn Fn behind a reference
  SCOPE END         scope     end of block - early `return` - `break` - `continue` -
                              a match arm - a loop body (three iterations)
  ENV WIDTH         width     1..5 scalar captures - 1..3 FAT (str/String) captures -
                              mixed - the same boxed (heap env)
  CLOSURE PARAMS    param     a by-value owner unused - used - moved on - two of them -
                              by shared ref - by `&mut`

THE ORACLE IS A DESTRUCTOR COUNT PLUS A VALUE, NOT AN EXIT CODE.  Every owner
carries a distinct power-of-ten weight and a raw `*mut i64`; its destructor ADDS
its weight, so the final number is a SIGNATURE naming exactly which owners ran.
Every cell also reads a VALUE out of the construct into `r`, declared OUTSIDE the
construct and printed AFTER it -- a cell whose bindings are absent cannot pass.
Cells are additionally swept under `valgrind --leak-check=full` by run.sh -V.
"""
import os, sys, json

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
cells = []

PRE_D = """extern fn printf(fmt: *const u8, ...) -> i32;
fn emit(c: i64, v: i64) { unsafe { printf("count=%ld value=%ld\\n".as_ptr(), c, v); } }
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c + self.v; } } }"""


def emit_cell(cid, block, axes, count, value, why, uses, decls, body):
    src = "package %s;\n" % cid
    for u in sorted(set(uses)):
        src += u + "\n"
    src += PRE_D + "\n"
    seen = []
    for d in decls:
        if d not in seen:
            seen.append(d)
    for d in seen:
        src += d + "\n"
    src += ("fn main() -> i32 {\n"
            "    let mut n: i64 = 0i64;\n"
            "    let p: *mut i64 = &mut n;\n"
            "    let mut r: i64 = -1i64;\n"
            + body +
            "    emit(unsafe { n }, r);\n"
            "    return 0i32;\n}\n")
    open(os.path.join(OUT, cid + ".logos"), "w").write(src)
    c = dict(id=cid, block=block, expect_count=count, expect_value=value, why=why)
    c.update(axes)
    cells.append(c)


def D(w):
    return "D{v:%di64,c:p}" % w


W1 = "struct W1 { d: D, n: i64 }"
W2 = "struct W2 { d: D, e: D }"
OUTS = "struct Out { i: W1 }"
GEN = "struct G<T> { t: T }"
ENUM = "enum E { V(D), Z }"
RDE = "fn rd_e(e: &E) -> i64 { match e { E::V(ref d) => { return d.v; }, E::Z => { return -1i64; } } }"
TR = "trait Tr { fn get(self: &Self) -> i64; }\nimpl Tr for D { fn get(self: &D) -> i64 { return self.v; } }"
BOXED = "use logos.mem.boxed;"
STRING = "use logos.mem.string;"


def vecfn(k):
    return ("fn mkvec_%d(p: *mut i64) -> Vec<D> { let mut xs: Vec<D> = Vec::<D>::new(); "
            "xs.push(%s); xs.push(%s); return xs; }" % (k, D(k), D(10 * k)))


PAY = {}


def payload(name, ty, mk, read, base, decls=(), uses=(), part=None, partw=0):
    PAY[name] = dict(name=name, ty=ty, mk=mk, read=read, base=base,
                     decls=list(decls), uses=list(uses), part=part, partw=partw)


payload("plain",  "D",        lambda k: D(k),                                 lambda x: "%s.v" % x, 1)
payload("field",  "W1",       lambda k: "W1{d:%s, n:5i64}" % D(k),            lambda x: "%s.d.v" % x, 1, [W1],
        part=lambda x: "%s.d" % x, partw=1)
payload("two",    "W2",       lambda k: "W2{d:%s, e:%s}" % (D(k), D(10 * k)), lambda x: "%s.d.v" % x, 11, [W2],
        part=lambda x: "%s.d" % x, partw=1)
payload("tuple",  "(D, i64)", lambda k: "(%s, 5i64)" % D(k),                  lambda x: "%s.0.v" % x, 1,
        part=lambda x: "%s.0" % x, partw=1)
payload("tuple2", "(D, D)",   lambda k: "(%s, %s)" % (D(k), D(10 * k)),       lambda x: "%s.0.v" % x, 11,
        part=lambda x: "%s.0" % x, partw=1)
payload("array",  "[D; 2]",   lambda k: "[%s, %s]" % (D(k), D(10 * k)),       lambda x: "%s[0].v" % x, 11)
payload("arr3",   "[D; 3]",   lambda k: "[%s, %s, %s]" % (D(k), D(10 * k), D(100 * k)),
        lambda x: "%s[0].v" % x, 111)
payload("vec",    "Vec<D>",   lambda k: "mkvec_%d(p)" % k,                    lambda x: "%s.borrow(0u64).v" % x, 11,
        [vecfn(1), vecfn(1000)])
payload("box",    "Box<D>",   lambda k: "Box::new(%s)" % D(k),                lambda x: "%s.v" % x, 1, [], [BOXED])
payload("enum",   "E",        lambda k: "E::V(%s)" % D(k),                    lambda x: "rd_e(&%s)" % x, 1, [ENUM, RDE])
payload("nested", "Out",      lambda k: "Out{i: W1{d:%s, n:5i64}}" % D(k),    lambda x: "%s.i.d.v" % x, 1, [W1, OUTS],
        part=lambda x: "%s.i.d" % x, partw=1)
payload("generic", "G<D>",    lambda k: "G::<D> { t: %s }" % D(k),            lambda x: "%s.t.v" % x, 1, [GEN],
        part=lambda x: "%s.t" % x, partw=1)
payload("dyn",    "Box<dyn Tr>", lambda k: "Box::new(%s) as Box<dyn Tr>" % D(k), lambda x: "%s.get()" % x, 1,
        [TR], [BOXED])
payload("boxfield", "Box<W1>", lambda k: "Box::new(W1{d:%s, n:5i64})" % D(k), lambda x: "%s.d.v" % x, 1, [W1], [BOXED])

PORDER = ["plain", "field", "two", "tuple", "tuple2", "array", "arr3", "vec", "box",
          "enum", "nested", "generic", "dyn", "boxfield"]

# =====================================================================  BLOCK A
# CAPTURE MODE x PAYLOAD.  The closure is created inside an inner block; by the
# end of that block every owner built in it must have been destroyed EXACTLY once.
for pn in PORDER:
    P = PAY[pn]
    ty, mk, rd, base = P['ty'], P['mk'], P['read'], P['base']
    ax = dict(pay=pn, shape="Fn", scope="block")

    emit_cell("a_ref_read_" + pn, "A", dict(ax, how="byref", go="called"), base, 1,
              "a non-`move` closure BORROWS the capture; the outer binding still owns it and "
              "drops its whole tree once at the end of the block",
              P['uses'], P['decls'],
              "    { let x: %s = %s;\n      let f = || -> i64 { return %s; };\n      r = f(); }\n"
              % (ty, mk(1), rd("x")))

    emit_cell("a_move_read_" + pn, "A", dict(ax, how="move_read", go="called"), base, 1,
              "a `move` closure takes ownership; whether the drop is charged to the closure or "
              "to the source scope, the value is destroyed exactly once by the end of the block",
              P['uses'], P['decls'],
              "    { let x: %s = %s;\n      let f = move || -> i64 { return %s; };\n      r = f(); }\n"
              % (ty, mk(1), rd("x")))

    emit_cell("a_move_consume_" + pn, "A", dict(ax, how="move_consume", go="called", shape="FnOnce"), base, 1,
              "the `move` closure owns the capture and its body MOVES it into a body local, which "
              "drops at the body's end; the closure is called exactly once, so exactly one drop",
              P['uses'], P['decls'],
              "    { let x: %s = %s;\n      let k: i64 = %s;\n"
              "      let f = move || -> i64 { let t: %s = x; return %s; };\n      r = f(); if r != k { r = -9i64; } }\n"
              % (ty, mk(1), rd("x"), ty, rd("t")))

    emit_cell("a_move_nocall_read_" + pn, "A", dict(ax, how="move_read", go="never_called"), base, 1,
              "a `move` closure that is NEVER CALLED still OWNS its capture; dropping the closure "
              "value at the end of the block must destroy the capture exactly once",
              P['uses'], P['decls'],
              "    { let x: %s = %s;\n      r = %s;\n      let f = move || -> i64 { return %s; }; }\n"
              % (ty, mk(1), rd("x"), rd("x")))

    emit_cell("a_move_nocall_consume_" + pn, "A", dict(ax, how="move_consume", go="never_called", shape="FnOnce"), base, 1,
              "the body would move the capture out, but the closure is NEVER CALLED, so the body is "
              "no drop site at all; the closure value owns the capture and destroys it at block end",
              P['uses'], P['decls'],
              "    { let x: %s = %s;\n      r = %s;\n      let f = move || { let t: %s = x; }; }\n"
              % (ty, mk(1), rd("x"), ty))

# =====================================================================  BLOCK B
# RFC-2229 NARROW FIELD CAPTURE.  Only the named field is captured; the root
# stays put.  Either way the whole tree is destroyed exactly once.
for pn in PORDER:
    P = PAY[pn]
    if not P['part']:
        continue
    ty, mk, rd, base = P['ty'], P['mk'], P['read'], P['base']
    ax = dict(pay=pn, shape="Fn", scope="block", how="narrow")
    emit_cell("b_narrow_consume_call_" + pn, "B", dict(ax, go="called"), base, 1,
              "a narrow `move` capture of one droppable FIELD: the field is owed to the closure/env, "
              "the rest of the root to the root's scope; total is still one destruction of each owner",
              P['uses'], P['decls'],
              "    { let x: %s = %s;\n      let k: i64 = %s;\n"
              "      let f = move || -> i64 { let t: D = %s; return t.v; };\n      r = f(); if r != k { r = -9i64; } }\n"
              % (ty, mk(1), rd("x"), P['part']("x")))
    emit_cell("b_narrow_consume_nocall_" + pn, "B", dict(ax, go="never_called"), base, 1,
              "the same narrow capture with the closure NEVER CALLED: the env owns the field and must "
              "destroy it when the closure dies; the root's scope destroys the rest",
              P['uses'], P['decls'],
              "    { let x: %s = %s;\n      r = %s;\n"
              "      let f = move || { let t: D = %s; }; }\n"
              % (ty, mk(1), rd("x"), P['part']("x")))
    emit_cell("b_narrow_read_nocall_" + pn, "B", dict(ax, go="never_called", how="narrow_read"), base, 1,
              "a narrow capture the body only READS, never called: the root still owns everything and "
              "destroys its whole tree once at block end",
              P['uses'], P['decls'],
              "    { let x: %s = %s;\n      r = %s;\n"
              "      let f = move || -> i64 { return %s.v; }; }\n"
              % (ty, mk(1), rd("x"), P['part']("x")))

# =====================================================================  BLOCK C
# WHERE THE CLOSURE GOES.  Payload fixed at `plain` (weight 1) plus `field`
# and `box`, so the axis is the destination, not the type.
APF = "fn ap<F: Fn() -> i64>(f: F) -> i64 { return f(); }"
APR = "fn apr<F: Fn() -> i64>(f: &F) -> i64 { return f(); }"
APM = "fn apm<F: FnMut() -> i64>(mut f: F) -> i64 { return f() + f(); }"
APMR = "fn apmr<F: FnMut() -> i64>(f: &mut F) -> i64 { return f(); }"
APO = "fn apo<F: FnOnce() -> i64>(f: F) -> i64 { return f(); }"

for pn in ["plain", "field", "box"]:
    P = PAY[pn]
    ty, mk, rd, base = P['ty'], P['mk'], P['read'], P['base']
    ax = dict(pay=pn, how="move_read", scope="block")

    emit_cell("c_stored_called_" + pn, "C", dict(ax, go="stored", shape="Fn"), base, 1,
              "the closure is bound to a local and called through that local: one owner, one drop at block end",
              P['uses'], P['decls'],
              "    { let x: %s = %s;\n      let f = move || -> i64 { return %s; };\n"
              "      let g = &f;\n      r = f(); }\n" % (ty, mk(1), rd("x")))

    emit_cell("c_called_twice_" + pn, "C", dict(ax, go="called_twice", shape="Fn"), base, 2,
              "an `Fn` closure called TWICE reads its capture twice and is still destroyed once; "
              "calling it must not drop the capture",
              P['uses'], P['decls'],
              "    { let x: %s = %s;\n      let f = move || -> i64 { return %s; };\n"
              "      r = f() + f(); }\n" % (ty, mk(1), rd("x")))

    emit_cell("c_pass_byvalue_" + pn, "C", dict(ax, go="param_byvalue", shape="F: Fn"), base, 1,
              "the closure is moved into a generic `F: Fn` parameter; the callee's frame owns it and "
              "destroys the capture exactly once",
              P['uses'], P['decls'] + [APF],
              "    { let x: %s = %s;\n      let f = move || -> i64 { return %s; };\n"
              "      r = ap(f); }\n" % (ty, mk(1), rd("x")))

    emit_cell("c_pass_ref_" + pn, "C", dict(ax, go="param_ref", shape="&F"), base, 1,
              "the closure is LENT to `&F`; the caller keeps ownership and destroys the capture once at block end",
              P['uses'], P['decls'] + [APR],
              "    { let x: %s = %s;\n      let f = move || -> i64 { return %s; };\n"
              "      r = apr(&f); }\n" % (ty, mk(1), rd("x")))

    emit_cell("c_pass_mut_" + pn, "C", dict(ax, go="param_mutref", shape="&mut F"), base, 1,
              "the closure is lent to `&mut F`; ownership stays with the caller, one drop at block end",
              P['uses'], P['decls'] + [APMR],
              "    { let x: %s = %s;\n      let mut f = move || -> i64 { return %s; };\n"
              "      r = apmr(&mut f); }\n" % (ty, mk(1), rd("x")))

    emit_cell("c_pass_fnmut_" + pn, "C", dict(ax, go="param_byvalue", shape="F: FnMut"), base, 2,
              "an `FnMut` bound taking the closure by value and calling it twice: two reads, one destruction",
              P['uses'], P['decls'] + [APM],
              "    { let x: %s = %s;\n      let f = move || -> i64 { return %s; };\n"
              "      r = apm(f); }\n" % (ty, mk(1), rd("x")))

    emit_cell("c_pass_fnonce_" + pn, "C", dict(ax, go="param_byvalue", shape="F: FnOnce"), base, 1,
              "an `FnOnce` bound taking the closure by value: the callee owns and destroys it, one drop",
              P['uses'], P['decls'] + [APO],
              "    { let x: %s = %s;\n      let f = move || -> i64 { return %s; };\n"
              "      r = apo(f); }\n" % (ty, mk(1), rd("x")))

    emit_cell("c_box_dyn_local_" + pn, "C", dict(ax, go="box_dyn_local", shape="Box<dyn Fn>"), base, 1,
              "the closure is boxed as `Box<dyn Fn() -> i64>` in the SAME frame and called; the box owns "
              "the env, so the capture is destroyed exactly once when the box dies",
              P['uses'] + [BOXED], P['decls'],
              "    { let x: %s = %s;\n"
              "      let f: Box<dyn Fn() -> i64> = Box::new(move || -> i64 { return %s; });\n"
              "      r = f(); }\n" % (ty, mk(1), rd("x")))

    emit_cell("c_box_dyn_escape_" + pn, "C", dict(ax, go="box_dyn_escape", shape="Box<dyn Fn>", scope="escape"), base, 1,
              "the closure ESCAPES its defining fn as `Box<dyn Fn() -> i64>` and is called after that fn "
              "returned; the heap env must still hold a live capture and destroy it when the box dies",
              P['uses'] + [BOXED], P['decls'] +
              ["fn mkc(p: *mut i64) -> Box<dyn Fn() -> i64> { let x: %s = %s; return Box::new(move || -> i64 { return %s; }); }"
               % (ty, mk(1), rd("x"))],
              "    { let f: Box<dyn Fn() -> i64> = mkc(p);\n      r = f(); }\n")

    emit_cell("c_never_called_" + pn, "C", dict(ax, go="never_called", shape="Fn"), base, 1,
              "the closure is created and never called; it owns the capture and destroys it at block end",
              P['uses'], P['decls'],
              "    { let x: %s = %s;\n      r = %s;\n      let f = move || -> i64 { return %s; }; }\n"
              % (ty, mk(1), rd("x"), rd("x")))

# =====================================================================  BLOCK D
# WHERE THE SCOPE ENDS.
for pn in ["plain", "field"]:
    P = PAY[pn]
    ty, mk, rd, base = P['ty'], P['mk'], P['read'], P['base']
    ax = dict(pay=pn, how="move_read", shape="Fn", go="called")

    emit_cell("d_early_return_" + pn, "D", dict(ax, scope="return"), base, 1,
              "the fn RETURNS with a live `move` closure in scope: the capture must be destroyed on the "
              "return path exactly once",
              P['uses'], P['decls'] +
              ["fn er(p: *mut i64) -> i64 { let x: %s = %s; let f = move || -> i64 { return %s; };"
               " if f() > 0i64 { return f(); } return 0i64; }" % (ty, mk(1), rd("x"))],
              "    r = er(p);\n")

    emit_cell("d_break_" + pn, "D", dict(ax, scope="break"), base, 1,
              "a `move` closure created inside a loop body that BREAKS on the first iteration: one "
              "creation, one destruction",
              P['uses'], P['decls'],
              "    { let mut i: i64 = 0i64;\n      while i < 3i64 { let x: %s = %s;"
              " let f = move || -> i64 { return %s; }; r = f(); break; } }\n" % (ty, mk(1), rd("x")))

    emit_cell("d_loop3_" + pn, "D", dict(ax, scope="loop3"), base * 3, 1,
              "a `move` closure created and destroyed on each of three iterations: three whole destructions",
              P['uses'], P['decls'],
              "    { let mut i: i64 = 0i64;\n      while i < 3i64 { let x: %s = %s;"
              " let f = move || -> i64 { return %s; }; r = f(); i = i + 1i64; } }\n" % (ty, mk(1), rd("x")))

    emit_cell("d_continue_" + pn, "D", dict(ax, scope="continue"), base * 3, 1,
              "a `move` closure created before a `continue` on every one of three iterations: three destructions",
              P['uses'], P['decls'],
              "    { let mut i: i64 = 0i64;\n      while i < 3i64 { let x: %s = %s;"
              " let f = move || -> i64 { return %s; }; r = f(); i = i + 1i64; continue; } }\n" % (ty, mk(1), rd("x")))

    emit_cell("d_match_arm_" + pn, "D", dict(ax, scope="match_arm"), base, 1,
              "a `move` closure created inside a match arm: the arm's block end destroys it once",
              P['uses'], P['decls'],
              "    { let k: i64 = 1i64;\n      match k { 1i64 => { let x: %s = %s;"
              " let f = move || -> i64 { return %s; }; r = f(); }, _ => { r = -2i64; } } }\n"
              % (ty, mk(1), rd("x")))

    emit_cell("d_nested_block_" + pn, "D", dict(ax, scope="nested_block"), base, 1,
              "a `move` closure created in a nested block inside the outer block: destroyed at the INNER "
              "block's end, once",
              P['uses'], P['decls'],
              "    { { let x: %s = %s; let f = move || -> i64 { return %s; }; r = f(); } }\n"
              % (ty, mk(1), rd("x")))

# =====================================================================  BLOCK E
# ENV WIDTH.  Value oracle only (scalars/str do not drop) plus a String variant
# that also carries a count.  The env-overflow row lives on this axis.
def scalars(k):
    return "".join("      let s%d: i64 = %di64;\n" % (i, 10 ** i) for i in range(k))


def scalar_sum(k):
    return sum(10 ** i for i in range(k))


def scalar_expr(k):
    return " + ".join("s%d" % i for i in range(k))


for k in range(1, 6):
    emit_cell("e_scalars_%d" % k, "E", dict(width=k, how="move_read", pay="i64", go="called", shape="Fn"),
              0, scalar_sum(k),
              "%d scalar `Copy` captures in a stack env: nothing to destroy, and every capture must read "
              "back its own value" % k, [], [],
              "    {\n%s      let f = move || -> i64 { return %s; };\n      r = f(); }\n"
              % (scalars(k), scalar_expr(k)))
    emit_cell("e_scalars_boxed_%d" % k, "E", dict(width=k, how="move_read", pay="i64", go="box_dyn_escape", shape="Box<dyn Fn>"),
              0, scalar_sum(k),
              "the same %d scalar captures behind a RETURNED `Box<dyn Fn>` (heap env): the env must be "
              "sized for every capture and every value must survive the escape" % k,
              [BOXED],
              ["fn mk%d(p: *mut i64) -> Box<dyn Fn() -> i64> {\n%s      return Box::new(move || -> i64 { return %s; }); }"
               % (k, scalars(k), scalar_expr(k))],
              "    { let f: Box<dyn Fn() -> i64> = mk%d(p);\n      r = f(); }\n" % k)

FAT = {1: ["abc"], 2: ["abc", "de"], 3: ["abc", "de", "f"]}
for k, ws in FAT.items():
    decl = "".join('      let s%d: str = "%s";\n' % (i, w) for i, w in enumerate(ws))
    expr = " + ".join("(s%d.len() as i64)" % i for i in range(k))
    tot = sum(len(w) for w in ws)
    emit_cell("e_fat_%d" % k, "E", dict(width=k, how="move_read", pay="str", go="called", shape="Fn"),
              0, tot,
              "%d FAT {ptr,len} `str` captures in a stack env: the env must be sized in BYTES, not in "
              "words, and each length must read back" % k, [], [],
              "    {\n%s      let f = move || -> i64 { return %s; };\n      r = f(); }\n" % (decl, expr))
    emit_cell("e_fat_boxed_%d" % k, "E", dict(width=k, how="move_read", pay="str", go="box_dyn_escape", shape="Box<dyn Fn>"),
              0, tot,
              "the same %d fat `str` captures behind a RETURNED `Box<dyn Fn>`: the malloc'd env must be "
              "as large as the capture struct; a short env is a heap buffer overflow" % k,
              [BOXED],
              ["fn mf%d(p: *mut i64) -> Box<dyn Fn() -> i64> {\n%s      return Box::new(move || -> i64 { return %s; }); }"
               % (k, decl, expr)],
              "    { let f: Box<dyn Fn() -> i64> = mf%d(p);\n      r = f(); }\n" % k)

for k in [1, 2, 3]:
    decl = "".join("      let d%d: D = %s;\n" % (i, D(10 ** i)) for i in range(k))
    expr = " + ".join("d%d.v" % i for i in range(k))
    tot = sum(10 ** i for i in range(k))
    emit_cell("e_owners_%d" % k, "E", dict(width=k, how="move_read", pay="plain", go="called", shape="Fn"),
              tot, tot,
              "%d droppable captures moved into one `move` closure: each is destroyed exactly once" % k,
              [], [],
              "    {\n%s      let f = move || -> i64 { return %s; };\n      r = f(); }\n" % (decl, expr))
    emit_cell("e_owners_boxed_%d" % k, "E", dict(width=k, how="move_read", pay="plain", go="box_dyn_escape", shape="Box<dyn Fn>"),
              tot, tot,
              "%d droppable captures behind a RETURNED `Box<dyn Fn>`: the heap env owns them and its drop "
              "glue must destroy each exactly once" % k, [BOXED],
              ["fn mo%d(p: *mut i64) -> Box<dyn Fn() -> i64> {\n%s      return Box::new(move || -> i64 { return %s; }); }"
               % (k, decl, expr)],
              "    { let f: Box<dyn Fn() -> i64> = mo%d(p);\n      r = f(); }\n" % k)

emit_cell("e_mixed_scalar_fat", "E", dict(width=3, how="move_read", pay="mixed", go="called", shape="Fn"),
          0, 1 + 3,
          "a scalar and a fat capture in one env: field offsets must follow the real sizes, so both read back",
          [], [],
          '    {\n      let a: i64 = 1i64;\n      let s: str = "abc";\n'
          '      let f = move || -> i64 { return a + (s.len() as i64); };\n      r = f(); }\n')
emit_cell("e_mixed_owner_fat", "E", dict(width=2, how="move_read", pay="mixed", go="called", shape="Fn"),
          1, 1 + 3,
          "a droppable owner and a fat `str` in one env: the owner is destroyed once and the str's length "
          "still reads back, so the drop glue must walk the right field",
          [], [],
          '    {\n      let d: D = %s;\n      let s: str = "abc";\n'
          '      let f = move || -> i64 { return d.v + (s.len() as i64); };\n      r = f(); }\n' % D(1))

# =====================================================================  BLOCK F
# CLOSURE PARAMETERS.
for pn in ["plain", "field", "box", "two"]:
    P = PAY[pn]
    ty, mk, rd, base = P['ty'], P['mk'], P['read'], P['base']
    ax = dict(pay=pn, how="param", shape="FnOnce", scope="block")
    emit_cell("f_param_unused_" + pn, "F", dict(ax, go="param_byvalue"), base, 1,
              "a closure BY-VALUE parameter the body never uses: the closure body's epilogue owns it and "
              "must destroy it exactly like a function's unconsumed by-value parameter",
              P['uses'], P['decls'] +
              ["fn cal<F>(x: %s, f: F) -> i64 where F: FnOnce(%s) -> i64 { return f(x); }" % (ty, ty)],
              "    { r = cal(%s, |y: %s| -> i64 { return 1i64; }); }\n" % (mk(1), ty))
    emit_cell("f_param_used_" + pn, "F", dict(ax, go="param_byvalue"), base, 1,
              "a closure by-value parameter the body READS: read, then destroyed once at the body's end",
              P['uses'], P['decls'] +
              ["fn cal<F>(x: %s, f: F) -> i64 where F: FnOnce(%s) -> i64 { return f(x); }" % (ty, ty)],
              "    { r = cal(%s, |y: %s| -> i64 { return %s; }); }\n" % (mk(1), ty, rd("y")))
    emit_cell("f_param_moved_on_" + pn, "F", dict(ax, go="param_moved"), base, 1,
              "a closure by-value parameter MOVED into a callee: the callee's frame destroys it, the "
              "closure body must not",
              P['uses'], P['decls'] +
              ["fn cal<F>(x: %s, f: F) -> i64 where F: FnOnce(%s) -> i64 { return f(x); }" % (ty, ty),
               "fn eat(x: %s) -> i64 { return %s; }" % (ty, rd("x"))],
              "    { r = cal(%s, |y: %s| -> i64 { return eat(y); }); }\n" % (mk(1), ty))
    emit_cell("f_param_ref_" + pn, "F", dict(ax, go="param_ref"), base, 1,
              "a closure parameter taken by SHARED REFERENCE: the caller still owns the value and "
              "destroys it once",
              P['uses'], P['decls'] +
              ["fn calr<F>(x: &%s, f: F) -> i64 where F: Fn(&%s) -> i64 { return f(x); }" % (ty, ty)],
              "    { let x: %s = %s;\n      r = calr(&x, |y: &%s| -> i64 { return %s; }); }\n"
              % (ty, mk(1), ty, rd("(*y)")))

emit_cell("f_param_two_byvalue", "F", dict(pay="plain", how="param", shape="FnOnce", go="param_byvalue", scope="block"),
          1001, 1,
          "TWO unused by-value owner parameters of one closure: both are destroyed at the body's end, "
          "so the signature names both", [],
          ["fn cal2<F>(a: D, b: D, f: F) -> i64 where F: FnOnce(D, D) -> i64 { return f(a, b); }"],
          "    { r = cal2(%s, %s, |u: D, w: D| -> i64 { return 1i64; }); }\n" % (D(1), D(1000)))

emit_cell("f_param_mixed_use", "F", dict(pay="plain", how="param", shape="FnOnce", go="param_byvalue", scope="block"),
          1001, 1000,
          "two by-value owner parameters, one READ and one ignored: both are still destroyed once", [],
          ["fn cal2<F>(a: D, b: D, f: F) -> i64 where F: FnOnce(D, D) -> i64 { return f(a, b); }"],
          "    { r = cal2(%s, %s, |u: D, w: D| -> i64 { return w.v; }); }\n" % (D(1), D(1000)))

emit_cell("f_param_and_capture", "F", dict(pay="plain", how="param_and_capture", shape="FnOnce", go="param_byvalue", scope="block"),
          1001, 1,
          "a closure with BOTH an owned capture and an unused by-value parameter: the capture is destroyed "
          "with the closure and the parameter at the body's end — the signature must name both", [],
          ["fn cal<F>(x: D, f: F) -> i64 where F: FnOnce(D) -> i64 { return f(x); }"],
          "    { let c: D = %s;\n      r = cal(%s, move |y: D| -> i64 { return c.v; }); }\n" % (D(1), D(1000)))

# =====================================================================  BLOCK G
# TRAIT SHAPE / CALL FORM.
emit_cell("g_fnptr_noncapturing", "G", dict(pay="i64", how="none", shape="fnptr", go="called", scope="block"),
          0, 7,
          "a NON-CAPTURING closure coerced to a bare `fn(i64) -> i64` pointer: no env at all, and the call "
          "must reach the body", [],
          ["fn takes_fp(f: fn(i64) -> i64) -> i64 { return f(7i64); }"],
          "    { let f: fn(i64) -> i64 = |x: i64| -> i64 { return x; };\n      r = takes_fp(f); }\n")

emit_cell("g_dyn_fn_behind_ref", "G", dict(pay="plain", how="move_read", shape="&dyn Fn", go="param_ref", scope="block"),
          1, 1,
          "a boxed closure lent as `&dyn Fn() -> i64`: the borrow does not own the env, so the box still "
          "destroys the capture exactly once", [BOXED],
          ["fn call_dyn(f: &dyn Fn() -> i64) -> i64 { return f(); }"],
          "    { let x: D = %s;\n"
          "      let b: Box<dyn Fn() -> i64> = Box::new(move || -> i64 { return x.v; });\n"
          "      r = call_dyn(&b); }\n" % D(1))

emit_cell("g_fnmut_counter", "G", dict(pay="i64", how="mutref", shape="FnMut", go="param_byvalue", scope="block"),
          0, 3,
          "an `FnMut` closure mutating a captured outer local through the env: after three calls the OUTER "
          "variable must read 3, so the capture is by mutable reference and not a copy", [],
          ["fn thrice<F: FnMut()>(mut f: F) { f(); f(); f(); }"],
          "    { let mut k: i64 = 0i64;\n      thrice(|| { k = k + 1i64; });\n      r = k; }\n")

emit_cell("g_fnmut_ref_capture_owner", "G", dict(pay="plain", how="mutref", shape="FnMut", go="param_byvalue", scope="block"),
          1, 1,
          "an `FnMut` closure holding a `&mut` to an outer OWNER: the closure borrows, so the outer scope "
          "destroys the owner exactly once", [],
          ["fn once<F: FnMut() -> i64>(mut f: F) -> i64 { return f(); }"],
          "    { let mut x: D = %s;\n      r = once(|| -> i64 { x.v = x.v; return x.v; }); }\n" % D(1))

emit_cell("g_nested_closure", "G", dict(pay="plain", how="move_read", shape="nested", go="called", scope="block"),
          1, 1,
          "a capture carried through TWO closure layers: the inner closure captures the outer closure's "
          "capture, and the owner is destroyed exactly once", [], [],
          "    { let x: D = %s;\n      let f = move || -> i64 { let g = move || -> i64 { return x.v; }; return g(); };\n"
          "      r = f(); }\n" % D(1))

emit_cell("g_closure_captures_closure", "G", dict(pay="plain", how="move_read", shape="nested", go="called", scope="block"),
          1, 1,
          "a `move` closure capturing ANOTHER closure that itself owns a droppable capture: the owner is "
          "destroyed exactly once when the outer closure dies", [], [],
          "    { let x: D = %s;\n      let inner = move || -> i64 { return x.v; };\n"
          "      let outer = move || -> i64 { return inner(); };\n      r = outer(); }\n" % D(1))

emit_cell("g_closure_in_vec", "G", dict(pay="plain", how="move_read", shape="Box<dyn Fn>", go="in_vec", scope="block"),
          1, 1,
          "a boxed closure pushed into a `Vec`: the vector owns the box, and dropping the vector must "
          "destroy the capture exactly once", [BOXED],
          [],
          "    { let x: D = %s;\n      let mut v: Vec<Box<dyn Fn() -> i64>> = Vec::<Box<dyn Fn() -> i64>>::new();\n"
          "      v.push(Box::new(move || -> i64 { return x.v; }));\n      r = 1i64; }\n" % D(1))

emit_cell("g_generic_two_insts", "G", dict(pay="i64", how="none", shape="F: generic", go="called", scope="block"),
          0, 7,
          "a closure written inside a GENERIC fn instantiated at two types: each monomorphisation needs its "
          "own synthetic closure symbol", [],
          ["fn twice<T: Copy>(v: T) -> T { let c = |x: &T| -> T { return *x; }; return c(&v); }"],
          "    { let a: i64 = twice(3i64);\n      let b: i32 = twice(4i32);\n      r = a + (b as i64); }\n")

emit_cell("g_returned_impl_fn", "G", dict(pay="plain", how="move_read", shape="impl Fn", go="box_dyn_escape", scope="escape"),
          1, 1,
          "a closure returned as `impl Fn() -> i64`: it owns its capture and destroys it when the returned "
          "value dies", [],
          ["fn mki(p: *mut i64) -> impl Fn() -> i64 { let x: D = %s; return move || -> i64 { return x.v; }; }" % D(1)],
          "    { let f = mki(p);\n      r = f(); }\n")

emit_cell("g_box_dyn_fnmut", "G", dict(pay="plain", how="move_read", shape="Box<dyn FnMut>", go="box_dyn_local", scope="block"),
          1, 2,
          "a `Box<dyn FnMut() -> i64>` called twice: two reads, one destruction of the capture", [BOXED], [],
          "    { let x: D = %s;\n"
          "      let mut f: Box<dyn FnMut() -> i64> = Box::new(move || -> i64 { return x.v; });\n"
          "      r = f() + f(); }\n" % D(1))

emit_cell("g_box_dyn_fnonce", "G", dict(pay="plain", how="move_consume", shape="Box<dyn FnOnce>", go="box_dyn_local", scope="block"),
          1, 1,
          "a `Box<dyn FnOnce() -> i64>` whose body consumes the capture, called once: exactly one destruction",
          [BOXED], [],
          "    { let x: D = %s;\n"
          "      let f: Box<dyn FnOnce() -> i64> = Box::new(move || -> i64 { let t: D = x; return t.v; });\n"
          "      r = f(); }\n" % D(1))

emit_cell("g_cast_box_dyn", "G", dict(pay="plain", how="move_read", shape="Box<dyn Fn>", go="cast", scope="block"),
          1, 1,
          "an explicit `as Box<dyn Fn() -> i64>` cast of a boxed closure: same ownership as the implicit "
          "coercion, one destruction", [BOXED], [],
          "    { let x: D = %s;\n      let f = move || -> i64 { return x.v; };\n"
          "      let g: Box<dyn Fn() -> i64> = Box::new(f) as Box<dyn Fn() -> i64>;\n      r = g(); }\n" % D(1))

emit_cell("g_closure_struct_field", "G", dict(pay="plain", how="move_read", shape="Box<dyn Fn>", go="in_struct", scope="block"),
          1, 1,
          "a boxed closure stored in a STRUCT FIELD: the struct owns the box and destroys the capture once "
          "at the struct's scope end", [BOXED],
          ["struct H { f: Box<dyn Fn() -> i64> }"],
          "    { let x: D = %s;\n"
          "      let h: H = H { f: Box::new(move || -> i64 { return x.v; }) };\n      r = (h.f)(); }\n" % D(1))

emit_cell("g_move_string_capture", "G", dict(pay="String", how="move_read", shape="Fn", go="called", scope="block"),
          0, 5,
          "a heap `String` moved into a `move` closure and read: the closure owns the buffer and must free "
          "it exactly once (valgrind is the oracle for the buffer; the length is the value)", [STRING], [],
          '    { let s: String = String::from("hello");\n'
          '      let f = move || -> i64 { return s.len(); };\n      r = f(); }\n')

emit_cell("g_move_string_nocall", "G", dict(pay="String", how="move_read", shape="Fn", go="never_called", scope="block"),
          0, 5,
          "the same `String` moved into a `move` closure that is NEVER CALLED: the buffer is still owed one "
          "free, and only valgrind can see it", [STRING], [],
          '    { let s: String = String::from("hello");\n      r = s.len();\n'
          '      let f = move || -> i64 { return s.len(); }; }\n')

emit_cell("g_selfrec_closure_param", "G", dict(pay="plain", how="param", shape="FnOnce", go="param_byvalue", scope="return"),
          1, 1,
          "a closure by-value owner parameter on a fn that self-calls in RETURN position with the branch NOT "
          "taken: the parameter must still be destroyed on the fall-through path", [],
          ["fn rec(x: D, b: bool) -> i64 { if b { return rec(x, false); } return x.v; }"],
          "    { r = rec(%s, false); }\n" % D(1))

# =====================================================================  BLOCK H
# THE BY-VALUE CLOSURE PARAMETER, BISECTED.  Block F's `-> i64` spelling is
# correct today while the queue row closure_byvalue_param_never_dropped (void
# return, empty body) is not, so the axis here is RETURN TYPE x BODY.
BV = "fn asb<F>(d: D, f: F) where F: FnOnce(D) { f(d); }"
BVI = "fn asbi<F>(d: D, f: F) -> i64 where F: FnOnce(D) -> i64 { return f(d); }"
BVS = "fn asbs<F>(d: D, f: F) where F: FnOnce(D) { let q: i64 = 0i64; f(d); }"

emit_cell("h_bv_void_empty", "H", dict(pay="plain", how="param", shape="FnOnce", go="param_byvalue", scope="void_body"),
          1000, -1,
          "an unused by-value owner parameter of a VOID closure with an EMPTY body: the body's epilogue "
          "owns it and must destroy it, exactly as a void `fn eat(d: D) {}` does", [], [BV],
          "    asb(%s, |x: D| {});\n" % D(1000))
emit_cell("h_bv_void_read", "H", dict(pay="plain", how="param", shape="FnOnce", go="param_byvalue", scope="void_body"),
          1000, -1,
          "the same VOID closure whose body READS the parameter: reading is not consuming, so the "
          "parameter is still destroyed at the body's end", [], [BV],
          "    asb(%s, |x: D| { let k: i64 = x.v; });\n" % D(1000))
emit_cell("h_bv_void_movelocal", "H", dict(pay="plain", how="param", shape="FnOnce", go="param_moved", scope="void_body"),
          1000, -1,
          "the VOID closure MOVES the parameter into a body local: the local's scope end destroys it once",
          [], [BV],
          "    asb(%s, |x: D| { let t: D = x; });\n" % D(1000))
emit_cell("h_bv_i64_empty", "H", dict(pay="plain", how="param", shape="FnOnce", go="param_byvalue", scope="i64_body"),
          1000, 7,
          "the same unused by-value owner parameter of a closure that RETURNS i64: identical ownership, "
          "the return type cannot change who destroys the parameter", [], [BVI],
          "    r = asbi(%s, |x: D| -> i64 { return 7i64; });\n" % D(1000))
emit_cell("h_bv_void_stmt", "H", dict(pay="plain", how="param", shape="FnOnce", go="param_byvalue", scope="void_body"),
          1000, -1,
          "the VOID closure with a non-empty body that does not mention the parameter: still destroyed at "
          "the body's end", [], [BVS],
          "    asbs(%s, |x: D| { let z: i64 = 1i64; });\n" % D(1000))
emit_cell("h_bv_void_two", "H", dict(pay="plain", how="param", shape="FnOnce", go="param_byvalue", scope="void_body"),
          1001, -1,
          "TWO unused by-value owner parameters of a VOID closure: both destroyed at the body's end", [],
          ["fn asb2<F>(a: D, b: D, f: F) where F: FnOnce(D, D) { f(a, b); }"],
          "    asb2(%s, %s, |x: D, y: D| {});\n" % (D(1), D(1000)))
emit_cell("h_bv_fn_control", "H", dict(pay="plain", how="param", shape="fn", go="param_byvalue", scope="void_body"),
          1000, -1,
          "THE FUNCTION CONTROL for the three cells above: a plain void `fn` with an unused by-value owner "
          "parameter. This door is correct today, so the epilogue that drops an unconsumed by-value "
          "parameter exists", [], ["fn eatv(d: D) {}"],
          "    eatv(%s);\n" % D(1000))

# =====================================================================  BLOCK I
# `impl Fn` RETURN, and the FAT-CAPTURE ENV, both bisected.
emit_cell("i_impl_fn_scalar", "I", dict(pay="i64", how="move_read", shape="impl Fn", go="impl_return", scope="escape"),
          0, 4242,
          "a capture-carrying closure returned as `impl Fn() -> i64` with a SCALAR capture: the returned "
          "closure value must be fully initialised and read back its capture", [],
          ["fn mis() -> impl Fn() -> i64 { let k: i64 = 4242i64; return move || -> i64 { return k; }; }"],
          "    { let f = mis();\n      r = f(); }\n")
emit_cell("i_impl_fn_nocap", "I", dict(pay="none", how="none", shape="impl Fn", go="impl_return", scope="escape"),
          0, 5,
          "a NON-capturing closure returned as `impl Fn() -> i64`: no env at all, so the returned value is "
          "just a fn pointer and a null env", [],
          ["fn min0() -> impl Fn() -> i64 { return || -> i64 { return 5i64; }; }"],
          "    { let f = min0();\n      r = f(); }\n")
emit_cell("i_impl_fn_owner_two", "I", dict(pay="plain", how="move_read", shape="impl Fn", go="impl_return", scope="escape"),
          1001, 1001,
          "TWO droppable captures returned as `impl Fn() -> i64`: each is destroyed exactly once when the "
          "returned closure dies", [],
          ["fn mio(p: *mut i64) -> impl Fn() -> i64 { let a: D = %s; let b: D = %s;"
           " return move || -> i64 { return a.v + b.v; }; }" % (D(1), D(1000))],
          "    { let f = mio(p);\n      r = f(); }\n")

emit_cell("i_fat_boxed_noannot_1", "I", dict(width=1, how="move_read", pay="str", go="box_dyn_escape", shape="Box<dyn Fn>"),
          0, 3,
          "ONE fat `str` capture behind a RETURNED `Box<dyn Fn>`, spelled WITHOUT a type annotation (the "
          "spelling the corpus uses): the malloc'd env must be as large as {glue, ptr, len}", [BOXED],
          ['fn fb1() -> Box<dyn Fn() -> i64> { let s = "abc"; return Box::new(move || -> i64 { return s.len() as i64; }); }'],
          "    { let f: Box<dyn Fn() -> i64> = fb1();\n      r = f(); }\n")
emit_cell("i_fat_boxed_noannot_2", "I", dict(width=2, how="move_read", pay="str", go="box_dyn_escape", shape="Box<dyn Fn>"),
          0, 5,
          "TWO fat `str` captures behind a RETURNED `Box<dyn Fn>`, unannotated: the env holds {glue, ptr, "
          "len, ptr, len} = 40 bytes", [BOXED],
          ['fn fb2() -> Box<dyn Fn() -> i64> { let s = "abc"; let t = "de";'
           ' return Box::new(move || -> i64 { return (s.len() + t.len()) as i64; }); }'],
          "    { let f: Box<dyn Fn() -> i64> = fb2();\n      r = f(); }\n")
emit_cell("i_fat_boxed_noannot_3", "I", dict(width=3, how="move_read", pay="str", go="box_dyn_escape", shape="Box<dyn Fn>"),
          0, 6,
          "THREE fat `str` captures behind a RETURNED `Box<dyn Fn>`, unannotated", [BOXED],
          ['fn fb3() -> Box<dyn Fn() -> i64> { let s = "abc"; let t = "de"; let u = "f";'
           ' return Box::new(move || -> i64 { return (s.len() + t.len() + u.len()) as i64; }); }'],
          "    { let f: Box<dyn Fn() -> i64> = fb3();\n      r = f(); }\n")
emit_cell("i_fat_local_noannot_2", "I", dict(width=2, how="move_read", pay="str", go="called", shape="Fn"),
          0, 5,
          "the SAME two fat captures NOT boxed and called in place (stack env): the control that separates "
          "the fat repr from the boxing", [], [],
          '    { let s = "abc"; let t = "de";\n      let f = move || -> i64 { return (s.len() + t.len()) as i64; };\n'
          "      r = f(); }\n")
emit_cell("i_fat_boxed_scalar_2", "I", dict(width=2, how="move_read", pay="i64", go="box_dyn_escape", shape="Box<dyn Fn>"),
          0, 11,
          "TWO SCALAR captures behind a RETURNED `Box<dyn Fn>`: the control that separates the fat repr "
          "from `Box<dyn Fn>` itself", [BOXED],
          ["fn sb2() -> Box<dyn Fn() -> i64> { let a: i64 = 1i64; let b: i64 = 10i64;"
           " return Box::new(move || -> i64 { return a + b; }); }"],
          "    { let f: Box<dyn Fn() -> i64> = sb2();\n      r = f(); }\n")
emit_cell("i_string_boxed_escape", "I", dict(width=1, how="move_read", pay="String", go="box_dyn_escape", shape="Box<dyn Fn>"),
          0, 5,
          "a heap `String` moved into a RETURNED `Box<dyn Fn>`: the heap env owns the String and its glue "
          "must free the buffer exactly once", [BOXED, STRING],
          ['fn gb() -> Box<dyn Fn() -> i64> { let s: String = String::from("hello");'
           ' return Box::new(move || -> i64 { return s.len(); }); }'],
          "    { let f: Box<dyn Fn() -> i64> = gb();\n      r = f(); }\n")

# =====================================================================  BLOCK J
# MUTATION, REBINDING, AND CONTAINERS.
emit_cell("j_move_scalar_copy", "J", dict(pay="i64", how="move_copy", shape="FnMut", go="called", scope="block"),
          0, 0,
          "a `move` closure MUTATING a captured scalar owns a COPY: the OUTER variable must be unchanged "
          "after three calls", [], ["fn thr<F: FnMut()>(mut f: F) { f(); f(); f(); }"],
          "    { let mut k: i64 = 0i64;\n      thr(move || { k = k + 1i64; });\n      r = k; }\n")
emit_cell("j_nonmove_mut_outer", "J", dict(pay="i64", how="mutref", shape="FnMut", go="called", scope="block"),
          0, 3,
          "a NON-`move` closure mutating a captured scalar borrows it: after three calls the OUTER "
          "variable reads 3", [], ["fn thr2<F: FnMut()>(mut f: F) { f(); f(); f(); }"],
          "    { let mut k: i64 = 0i64;\n      thr2(|| { k = k + 1i64; });\n      r = k; }\n")
emit_cell("j_mut_owner_field", "J", dict(pay="plain", how="mutref", shape="FnMut", go="called", scope="block"),
          9, 9,
          "a non-`move` closure writing a captured OWNER's field: the write must be visible outside and the "
          "owner destroyed once, so the destructor adds the NEW weight", [],
          ["fn one2<F: FnMut()>(mut f: F) { f(); }"],
          "    { let mut x: D = %s;\n      one2(|| { x.v = 9i64; });\n      r = x.v; }\n" % D(1))
emit_cell("j_closure_moved_to_local", "J", dict(pay="plain", how="move_read", shape="Fn", go="moved_local", scope="block"),
          1, 1,
          "the closure VALUE is moved to a second local and called there: exactly one owner of the env, so "
          "one destruction", [], [],
          "    { let x: D = %s;\n      let f = move || -> i64 { return x.v; };\n"
          "      let g = f;\n      r = g(); }\n" % D(1))
emit_cell("j_closure_reassigned", "J", dict(pay="plain", how="move_read", shape="Fn", go="reassigned", scope="block"),
          1001, 1000,
          "a closure local ASSIGNED OVER by a second closure: the first env's capture is destroyed at the "
          "assignment and the second at block end, so the signature names both", [], [],
          "    { let a: D = %s;\n      let b: D = %s;\n"
          "      let mut f = move || -> i64 { return a.v; };\n"
          "      f = move || -> i64 { return b.v; };\n      r = f(); }\n" % (D(1), D(1000)))
emit_cell("j_two_closures_one_ref", "J", dict(pay="plain", how="byref", shape="Fn", go="called", scope="block"),
          1, 2,
          "TWO non-`move` closures borrowing the same owner: neither owns it, so it is destroyed once at "
          "block end", [], [],
          "    { let x: D = %s;\n      let f = || -> i64 { return x.v; };\n"
          "      let g = || -> i64 { return x.v; };\n      r = f() + g(); }\n" % D(1))
emit_cell("j_closure_shadowed", "J", dict(pay="plain", how="move_read", shape="Fn", go="shadowed", scope="block"),
          1001, 1000,
          "a closure binding SHADOWED by a second closure: a shadowing let does not end the first "
          "binding's storage, so both envs are destroyed at block end", [], [],
          "    { let a: D = %s;\n      let b: D = %s;\n"
          "      let f = move || -> i64 { return a.v; };\n"
          "      let f = move || -> i64 { return b.v; };\n      r = f(); }\n" % (D(1), D(1000)))
emit_cell("j_closure_in_option", "J", dict(pay="plain", how="move_read", shape="Box<dyn Fn>", go="in_option", scope="block"),
          1, 1,
          "a boxed closure inside an `Option`: the Option owns the box and destroys the capture once",
          [BOXED], [],
          "    { let x: D = %s;\n"
          "      let o: Option<Box<dyn Fn() -> i64>> = Option::Some(Box::new(move || -> i64 { return x.v; }));\n"
          "      match o { Option::Some(ref g) => { r = 1i64; }, Option::None => { r = -2i64; } } }\n" % D(1))
emit_cell("j_closure_returns_ref_to_capture", "J", dict(pay="plain", how="byref", shape="Fn", go="called", scope="block"),
          1, 1,
          "a NON-`move` closure returning a shared reference to its capture: the capture lives in the "
          "enclosing frame, so the reference is not dangling and the owner drops once", [], [],
          "    { let x: D = %s;\n      let f = || -> &D { return &x; };\n"
          "      let q: &D = f();\n      r = q.v; }\n" % D(1))
emit_cell("j_move_closure_called_then_dropped", "J", dict(pay="plain", how="move_consume", shape="FnOnce", go="called", scope="block"),
          1001, 1,
          "an owned capture the body consumes AND a second owner in the same block: calling the closure "
          "once destroys the capture in the body, the other owner at block end", [], [],
          "    { let x: D = %s;\n      let y: D = %s;\n"
          "      let f = move || -> i64 { let t: D = x; return t.v; };\n"
          "      r = f() + y.v - 1000i64; }\n" % (D(1), D(1000)))

# =====================================================================  BLOCK K
# THE ESCAPE AXIS, VARIED BY SHAPE (rule 5): `escapes` is minted from the SHAPE
# OF THE HINT TYPE, so every escaping door that is not a `Box` wrapper is a
# separate probe.
emit_cell("k_impl_fn_called_twice", "K", dict(pay="i64", how="move_read", shape="impl Fn", go="impl_return", scope="escape"),
          0, 14,
          "a scalar-capturing closure returned as `impl Fn() -> i64` and called TWICE after its defining fn "
          "returned: both calls must read the capture, so the env may not live in the dead frame", [],
          ["fn mk2() -> impl Fn() -> i64 { let k: i64 = 7i64; return move || -> i64 { return k; }; }"],
          "    { let f = mk2();\n      r = f() + f(); }\n")
emit_cell("k_impl_fn_arg_ret", "K", dict(pay="i64", how="move_read", shape="impl Fn", go="impl_return", scope="escape"),
          0, 9,
          "a closure returned as `impl Fn(i64) -> i64` and applied to an argument after its defining fn "
          "returned", [],
          ["fn mk5() -> impl Fn(i64) -> i64 { let k: i64 = 7i64; return move |x: i64| -> i64 { return x + k; }; }"],
          "    { let f = mk5();\n      r = f(2i64); }\n")
emit_cell("k_impl_fn_nocap_twice", "K", dict(pay="none", how="none", shape="impl Fn", go="impl_return", scope="escape"),
          0, 10,
          "a NON-capturing closure returned as `impl Fn() -> i64` and called twice: the returned "
          "`{fn, env}` pair itself must be a value, not a pointer into the dead frame", [],
          ["fn mk6() -> impl Fn() -> i64 { return || -> i64 { return 5i64; }; }"],
          "    { let f = mk6();\n      r = f() + f(); }\n")
emit_cell("k_boxed_escape_twice", "K", dict(pay="i64", how="move_read", shape="Box<dyn Fn>", go="box_dyn_escape", scope="escape"),
          0, 14,
          "THE CONTROL for the three cells above: the same escaping closure through `Box<dyn Fn>`, which "
          "is the one hint shape that mints `escapes`", [BOXED],
          ["fn mk7() -> Box<dyn Fn() -> i64> { let k: i64 = 7i64; return Box::new(move || -> i64 { return k; }); }"],
          "    { let f: Box<dyn Fn() -> i64> = mk7();\n      r = f() + f(); }\n")
emit_cell("k_fnptr_escape", "K", dict(pay="none", how="none", shape="fnptr", go="fnptr_return", scope="escape"),
          0, 10,
          "a NON-capturing closure returned as a bare `fn() -> i64` pointer: no env, so the escape is "
          "trivially safe and this is the control that isolates the env", [],
          ["fn mk8() -> fn() -> i64 { return || -> i64 { return 5i64; }; }"],
          "    { let f: fn() -> i64 = mk8();\n      r = f() + f(); }\n")

# =====================================================================  BLOCK L
# MORE SHAPES: `&mut` captures, String parameters, FnOnce through a box, and
# the closure-drop group under a second owner.
emit_cell("l_param_string_void", "L", dict(pay="String", how="param", shape="FnOnce", go="param_byvalue", scope="void_body"),
          0, -1,
          "a heap `String` as an unused BY-VALUE parameter of a VOID closure: the buffer is owed one free "
          "and only valgrind can see it", [STRING],
          ["fn aps<F>(s: String, f: F) where F: FnOnce(String) { f(s); }"],
          '    aps(String::from("hello"), |x: String| {});\n')
emit_cell("l_param_string_i64", "L", dict(pay="String", how="param", shape="FnOnce", go="param_byvalue", scope="i64_body"),
          0, 5,
          "the same `String` parameter of a closure that RETURNS i64: the control that isolates the void "
          "return type", [STRING],
          ["fn apsi<F>(s: String, f: F) -> i64 where F: FnOnce(String) -> i64 { return f(s); }"],
          '    r = apsi(String::from("hello"), |x: String| -> i64 { return x.len(); });\n')
emit_cell("l_mutref_capture_owner_escape", "L", dict(pay="plain", how="mutref", shape="FnMut", go="called", scope="block"),
          9, 9,
          "a non-`move` `FnMut` closure writing a captured owner's field through the env, called twice: the "
          "owner is destroyed once, with the LAST value written", [],
          ["fn twice2<F: FnMut()>(mut f: F) { f(); f(); }"],
          "    { let mut x: D = %s;\n      twice2(|| { x.v = 9i64; });\n      r = x.v; }\n" % D(1))
emit_cell("l_boxed_fnonce_escape_consume", "L", dict(pay="plain", how="move_consume", shape="Box<dyn FnOnce>", go="box_dyn_escape", scope="escape"),
          1, 1,
          "an escaping `Box<dyn FnOnce() -> i64>` whose body CONSUMES its capture, called once after the "
          "defining fn returned: exactly one destruction", [BOXED],
          ["fn mko(p: *mut i64) -> Box<dyn FnOnce() -> i64> { let x: D = %s;"
           " return Box::new(move || -> i64 { let t: D = x; return t.v; }); }" % D(1)],
          "    { let f: Box<dyn FnOnce() -> i64> = mko(p);\n      r = f(); }\n")
emit_cell("l_capture_and_second_owner", "L", dict(pay="plain", how="move_read", shape="Fn", go="never_called", scope="block"),
          1001, 1000,
          "a `move` closure that is never called PLUS an untouched second owner in the same block: the "
          "signature must name both", [], [],
          "    { let x: D = %s;\n      let y: D = %s;\n"
          "      let f = move || -> i64 { return x.v; };\n      r = y.v; }\n" % (D(1), D(1000)))
emit_cell("l_move_capture_of_mutref", "L", dict(pay="i64", how="move_of_mutref", shape="FnMut", go="called", scope="block"),
          0, 5,
          "a `move` closure capturing a `&mut i64` BINDING: `move` transfers the reference, not the "
          "referent, so the outer variable must show the write", [],
          ["fn one3<F: FnMut()>(mut f: F) { f(); }"],
          "    { let mut k: i64 = 0i64;\n      { let q: &mut i64 = &mut k;\n"
          "        one3(move || { *q = 5i64; }); }\n      r = k; }\n")
emit_cell("l_closure_arg_to_two_calls", "L", dict(pay="plain", how="move_read", shape="&F", go="param_ref", scope="block"),
          1, 2,
          "one `move` closure lent to TWO separate `&F` calls: it is destroyed once at block end, not once "
          "per call", [],
          ["fn apr2<F: Fn() -> i64>(f: &F) -> i64 { return f(); }"],
          "    { let x: D = %s;\n      let f = move || -> i64 { return x.v; };\n"
          "      r = apr2(&f) + apr2(&f); }\n" % D(1))
emit_cell("l_closure_generic_bound_twice", "L", dict(pay="plain", how="move_read", shape="F: Fn", go="param_byvalue", scope="block"),
          1001, 1001,
          "TWO different `move` closures passed by value to the SAME generic `F: Fn` parameter: one "
          "monomorphisation each, and each capture destroyed exactly once", [],
          ["fn apg<F: Fn() -> i64>(f: F) -> i64 { return f(); }"],
          "    { let a: D = %s;\n      let b: D = %s;\n"
          "      r = apg(move || -> i64 { return a.v; }) + apg(move || -> i64 { return b.v; }); }\n"
          % (D(1), D(1000)))

json.dump(cells, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "cells.json"), "w"), indent=1)
print("%d cells -> %s" % (len(cells), OUT))
