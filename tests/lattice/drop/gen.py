#!/usr/bin/env python3
"""OWNERSHIP-AND-DROP LATTICE.

Axes, derived from the tree (the constructs the compiler actually accepts, probed
by hand before this file was written):

  WHAT IS DROPPED   payload    a bare Drop struct - a droppable FIELD - a struct with
                               TWO droppable fields - a tuple - a tuple of two owners -
                               a fixed array (2 and 3) - a Vec - a Box - an enum payload -
                               a nested struct - a generic G<T> at T=D - a Box<dyn Tr> -
                               a Box of a struct with a droppable field
  HOW OWNERSHIP     move       never moved - moved to a callee by value - returned out of
    LEAVES                     a fn - re-assigned over - shadowed - moved in ONE branch
                               (taken / not taken) - moved in a loop body - moved into a
                               `move` closure - partially moved (one field/element) -
                               lent by `&` (not moved)
  WHERE THE SCOPE   scope      end of block - early `return` - `break` - labelled `break`
    ENDS                       - `continue` - a diverging match arm - a nested block -
                               a match arm - a loop back edge - an `if let` body
  THROUGH WHAT      path       direct - through `&mut` (assignment drops the old value) -
                               through a field projection - through an index - through a
                               Box deref - through a pattern binding

  DROP ORDER        order      locals (reverse declaration) - struct fields (declaration) -
                               tuple elements - array elements - nested - temporaries

THE ORACLE IS A DESTRUCTOR COUNT, NOT AN EXIT CODE.  Every owner carries a distinct
power-of-ten weight and a raw `*mut i64`; its destructor ADDS its weight, so the final
number is a SIGNATURE naming exactly which owners ran.  The order block uses a second
counter type whose destructor does `*c = *c * 10 + v`, so the number is the SEQUENCE.
Every cell also reads a VALUE out of the construct into a variable declared OUTSIDE it
and prints it -- a cell whose bindings are entirely absent must not be able to pass.
"""
import os, sys, json

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
cells = []

PRE_D = """extern fn printf(fmt: *const u8, ...) -> i32;
fn emit(c: i64, v: i64) { unsafe { printf("count=%ld value=%ld\\n".as_ptr(), c, v); } }
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c + self.v; } } }"""

PRE_O = """extern fn printf(fmt: *const u8, ...) -> i32;
fn emit(c: i64, v: i64) { unsafe { printf("count=%ld value=%ld\\n".as_ptr(), c, v); } }
struct O { v: i64, c: *mut i64 }
impl Drop for O { fn drop(self: &mut O) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }"""


def emit(cid, block, axes, count, value, why, uses, decls, body, pre=PRE_D):
    src = "package %s;\n" % cid
    for u in uses:
        src += u + "\n"
    src += pre + "\n"
    for d in decls:
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


# ---------------------------------------------------------------- PAYLOADS
W1 = "struct W1 { d: D, n: i64 }"
W2 = "struct W2 { d: D, e: D }"
OUTS = "struct Out { i: W1 }"
GEN = "struct G<T> { t: T }"
ENUM = "enum E { V(D), Z }"
RDE = "fn rd_e(e: &E) -> i64 { match e { E::V(ref d) => { return d.v; }, E::Z => { return -1i64; } } }"
TR = "trait Tr { fn get(self: &Self) -> i64; }\nimpl Tr for D { fn get(self: &D) -> i64 { return self.v; } }"

BOXED = "use logos.mem.boxed;"


def vecfn(k):
    return ("fn mkvec_%d(p: *mut i64) -> Vec<D> { let mut xs: Vec<D> = Vec::<D>::new(); "
            "xs.push(%s); xs.push(%s); return xs; }" % (k, D(k), D(10 * k)))


PAY = {}


def payload(name, ty, mk, read, base, decls=(), uses=(), part=None, partw=0):
    PAY[name] = dict(name=name, ty=ty, mk=mk, read=read, base=base,
                     decls=list(decls), uses=list(uses), part=part, partw=partw)


# mk(k) -> expression building instance k;  read(x) -> i64 expression == k
payload("plain",  "D",        lambda k: D(k),                                   lambda x: "%s.v" % x, 1)
payload("field",  "W1",       lambda k: "W1{d:%s, n:5i64}" % D(k),              lambda x: "%s.d.v" % x, 1, [W1],
        part=lambda x: "%s.d" % x, partw=1)
payload("two",    "W2",       lambda k: "W2{d:%s, e:%s}" % (D(k), D(10 * k)),   lambda x: "%s.d.v" % x, 11, [W2],
        part=lambda x: "%s.d" % x, partw=1)
payload("tuple",  "(D, i64)", lambda k: "(%s, 5i64)" % D(k),                    lambda x: "%s.0.v" % x, 1,
        part=lambda x: "%s.0" % x, partw=1)
payload("tuple2", "(D, D)",   lambda k: "(%s, %s)" % (D(k), D(10 * k)),         lambda x: "%s.0.v" % x, 11,
        part=lambda x: "%s.0" % x, partw=1)
payload("array",  "[D; 2]",   lambda k: "[%s, %s]" % (D(k), D(10 * k)),         lambda x: "%s[0].v" % x, 11)
payload("arr3",   "[D; 3]",   lambda k: "[%s, %s, %s]" % (D(k), D(10 * k), D(100 * k)),
        lambda x: "%s[0].v" % x, 111)
payload("vec",    "Vec<D>",   lambda k: "mkvec_%d(p)" % k,                      lambda x: "%s.borrow(0u64).v" % x, 11,
        [vecfn(1), vecfn(1000)])
payload("box",    "Box<D>",   lambda k: "Box::new(%s)" % D(k),                  lambda x: "%s.v" % x, 1, [], [BOXED])
payload("enum",   "E",        lambda k: "E::V(%s)" % D(k),                      lambda x: "rd_e(&%s)" % x, 1, [ENUM, RDE])
payload("nested", "Out",      lambda k: "Out{i: W1{d:%s, n:5i64}}" % D(k),      lambda x: "%s.i.d.v" % x, 1, [W1, OUTS],
        part=lambda x: "%s.i.d" % x, partw=1)
payload("generic", "G<D>",    lambda k: "G::<D> { t: %s }" % D(k),              lambda x: "%s.t.v" % x, 1, [GEN],
        part=lambda x: "%s.t" % x, partw=1)
payload("dyn",    "Box<dyn Tr>", lambda k: "Box::new(%s) as Box<dyn Tr>" % D(k), lambda x: "%s.get()" % x, 1,
        [TR], [BOXED])
payload("boxfield", "Box<W1>", lambda k: "Box::new(W1{d:%s, n:5i64})" % D(k),   lambda x: "%s.d.v" % x, 1, [W1], [BOXED])

PORDER = ["plain", "field", "two", "tuple", "tuple2", "array", "arr3", "vec", "box",
          "enum", "nested", "generic", "dyn", "boxfield"]

# ------------------------------------------------------------- BLOCK A: payload x move
MOVES = {}


def move(name, fn, skip=()):
    MOVES[name] = (fn, set(skip))


def m_none(P):
    return [], "    { let x: %s = %s; r = %s; }\n" % (P['ty'], P['mk'](1), P['read']('x')), \
           P['base'], 1, "one owner built and never moved: its whole tree drops once at the end of the block"


def m_callee(P):
    d = ["fn eat(x: %s) -> i64 { return %s; }" % (P['ty'], P['read']('x'))]
    return d, "    { let x: %s = %s; r = eat(x); }\n" % (P['ty'], P['mk'](1)), \
           P['base'], 1, "moved by value into a callee: the callee's frame drops it exactly once, the caller must not"


def m_return(P):
    d = ["fn mk1(p: *mut i64) -> %s { let x: %s = %s; return x; }" % (P['ty'], P['ty'], P['mk'](1))]
    return d, "    { let x: %s = mk1(p); r = %s; }\n" % (P['ty'], P['read']('x')), \
           P['base'], 1, "returned out of the fn that built it: the producer must not drop it, the consumer must"


def m_reassign(P):
    return [], "    { let mut x: %s = %s; x = %s; r = %s; }\n" % (P['ty'], P['mk'](1), P['mk'](1000), P['read']('x')), \
           P['base'] * 1001, 1000, "assignment over a live owner drops the OLD value at the assignment; the new one at scope end"


def m_shadow(P):
    return [], "    { let x: %s = %s; let x: %s = %s; r = %s; }\n" % (P['ty'], P['mk'](1), P['ty'], P['mk'](1000), P['read']('x')), \
           P['base'] * 1001, 1000, "a shadowing let does NOT end the first binding's storage: both drop at the end of the block"


def m_branch_taken(P):
    d = ["fn eat(x: %s) -> i64 { return %s; }" % (P['ty'], P['read']('x'))]
    return d, "    { let x: %s = %s; if r < 0i64 { r = eat(x); } }\n" % (P['ty'], P['mk'](1)), \
           P['base'], 1, "moved in the branch that IS taken: the drop flag must be off at scope end, so exactly one drop"


def m_branch_nottaken(P):
    d = ["fn eat(x: %s) -> i64 { return %s; }" % (P['ty'], P['read']('x'))]
    return d, ("    { let x: %s = %s;\n      if r > 0i64 { r = eat(x); } else { r = %s; } }\n"
               % (P['ty'], P['mk'](1), P['read']('x'))), \
           P['base'], 1, "moved in the branch NOT taken: the drop flag must be on at scope end, so exactly one drop"


def m_loopmove(P):
    d = ["fn eat(x: %s) -> i64 { return %s; }" % (P['ty'], P['read']('x'))]
    return d, ("    { let x: %s = %s;\n      let mut i: i64 = 0i64;\n"
               "      while i < 3i64 { r = eat(x); break; }\n    }\n" % (P['ty'], P['mk'](1))), \
           P['base'], 1, "moved inside a loop whose body breaks unconditionally: the move is reachable once, one drop"


def m_loopcreate(P):
    return [], ("    { let mut i: i64 = 0i64;\n      while i < 3i64 { let x: %s = %s; r = %s; i = i + 1i64; }\n    }\n"
                % (P['ty'], P['mk'](1), P['read']('x'))), \
           P['base'] * 3, 1, "an owner built and dropped on every one of three iterations: three whole drops"


def m_closure(P):
    return [], ("    { let x: %s = %s;\n      let f = move || -> i64 { return %s; };\n      r = f(); }\n"
                % (P['ty'], P['mk'](1), P['read']('x'))), \
           P['base'], 1, "moved into a `move` closure: the closure owns it and drops it when the closure dies"


def m_byref(P):
    if P['name'] == 'dyn':
        return None
    d = ["fn look(x: &%s) -> i64 { return %s; }" % (P['ty'], P['read']("(*x)"))]
    return d, "    { let x: %s = %s; r = look(&x); }\n" % (P['ty'], P['mk'](1)), \
           P['base'], 1, "lent by shared reference and never moved: the owner still drops once at the end of the block"


def m_partial(P):
    if P['part'] is None:
        return None
    d = ["fn eatd(x: D) -> i64 { return x.v; }"]
    inner = P['part']('x')
    return d, "    { let x: %s = %s; r = eatd(%s); }\n" % (P['ty'], P['mk'](1), inner), \
           P['base'], 1, ("one component moved out by projection: the moved part drops in the callee, "
                          "every OTHER component still drops at scope end -- the total is unchanged, "
                          "so a whole-source move mark shows up as a MISSING part")


def m_never(P):
    return [], "    { let x: %s = %s; r = 42i64; }\n" % (P['ty'], P['mk'](1)), \
           P['base'], 42, "built and never read at all: it still drops once at the end of the block"


move("none", m_none)
move("callee", m_callee)
move("return", m_return)
move("reassign", m_reassign)
move("shadow", m_shadow)
move("branch_taken", m_branch_taken)
move("branch_nottaken", m_branch_nottaken)
move("loopmove", m_loopmove)
move("loopcreate", m_loopcreate)
move("closure", m_closure)
move("byref", m_byref)
move("partial", m_partial)
move("never", m_never)

MORDER = ["none", "callee", "return", "reassign", "shadow", "branch_taken", "branch_nottaken",
          "loopmove", "loopcreate", "closure", "byref", "partial", "never"]

for pn in PORDER:
    P = PAY[pn]
    for mn in MORDER:
        got = MOVES[mn][0](P)
        if got is None:
            continue
        d, body, cnt, val, why = got
        emit("a_%s_%s" % (pn, mn), "A", dict(payload=pn, move=mn), cnt, val, why,
             P['uses'], P['decls'] + d, body)

# ------------------------------------------------------------- BLOCK B: payload x scope end
SCOPES = {}


def scope(name, fn):
    SCOPES[name] = fn


def s_block(P):
    return [], "    { let x: %s = %s; r = %s; }\n" % (P['ty'], P['mk'](1), P['read']('x')), \
           P['base'], 1, "scope ends at the closing brace of a plain block"


def s_earlyret(P):
    d = ["fn f(p: *mut i64, k: i64) -> i64 { let x: %s = %s; if k > 0i64 { return %s; } return 0i64; }"
         % (P['ty'], P['mk'](1), P['read']('x'))]
    return d, "    r = f(p, 1i64);\n", P['base'], 1, \
           "scope ends at an early `return` taken before the fn's last statement: the local still drops"


def s_break(P):
    return [], ("    { let mut i: i64 = 0i64;\n      while i < 3i64 { let x: %s = %s; r = %s; break; }\n    }\n"
                % (P['ty'], P['mk'](1), P['read']('x'))), \
           P['base'], 1, "scope ends at an unlabelled `break`: the loop-body local drops on the way out, once"


def s_lblbreak(P):
    return [], ("    { let mut i: i64 = 0i64;\n      'outer: while i < 3i64 {\n"
                "        let x: %s = %s;\n        let mut j: i64 = 0i64;\n"
                "        while j < 2i64 { let y: %s = %s; r = %s; break 'outer; }\n"
                "        i = i + 1i64;\n      }\n    }\n"
                % (P['ty'], P['mk'](1), P['ty'], P['mk'](1000), P['read']('y'))), \
           P['base'] * 1001, 1000, \
           "a labelled `break` leaves TWO scopes: the inner local and the outer local both drop, once each"


def s_continue(P):
    return [], ("    { let mut i: i64 = 0i64;\n"
                "      while i < 2i64 { i = i + 1i64; let x: %s = %s; r = %s; continue; }\n    }\n"
                % (P['ty'], P['mk'](1), P['read']('x'))), \
           P['base'] * 2, 1, "`continue` ends the body scope on each of two iterations: two whole drops"


def s_divarm(P):
    d = ["fn f(p: *mut i64, k: i64) -> i64 { match k { 0i64 => { let x: %s = %s; return %s; }, _ => { return 7i64; } } }"
         % (P['ty'], P['mk'](1), P['read']('x'))]
    return d, "    r = f(p, 0i64);\n", P['base'], 1, \
           "a match arm that DIVERGES (returns) must still drop the local the arm built"


def s_nestblk(P):
    return [], "    { { let x: %s = %s; r = %s; } r = r + 0i64; }\n" % (P['ty'], P['mk'](1), P['read']('x')), \
           P['base'], 1, "the inner block ends before the outer one: the drop happens at the INNER brace"


def s_matcharm(P):
    return [], ("    { let k: i64 = 0i64;\n      match k { 0i64 => { let x: %s = %s; r = %s; }, _ => {} } }\n"
                % (P['ty'], P['mk'](1), P['read']('x'))), \
           P['base'], 1, "a match arm's block is a scope: the arm-local drops when the arm ends"


def s_loopback(P):
    return [], ("    { let mut i: i64 = 0i64;\n"
                "      while i < 3i64 { let x: %s = %s; r = %s; i = i + 1i64; }\n    }\n"
                % (P['ty'], P['mk'](1), P['read']('x'))), \
           P['base'] * 3, 1, "the loop back edge ends the body scope: one whole drop per iteration, three in all"


def s_iflet(P):
    return [], ("    { let o: E2 = E2::S(1i64);\n"
                "      if let E2::S(k) = o { let x: %s = %s; r = %s + k - 1i64; } }\n"
                % (P['ty'], P['mk'](1), P['read']('x'))), \
           P['base'], 1, "an `if let` body is a scope: the local built inside it drops at its end"


def s_ifelse(P):
    return [], ("    { if r < 0i64 { let x: %s = %s; r = %s; } else { r = 0i64; } }\n"
                % (P['ty'], P['mk'](1), P['read']('x'))), \
           P['base'], 1, "an `if` arm is a scope of its own: the local drops at the end of the taken arm"


scope("block", s_block)
scope("earlyret", s_earlyret)
scope("break", s_break)
scope("lblbreak", s_lblbreak)
scope("continue", s_continue)
scope("divarm", s_divarm)
scope("nestblk", s_nestblk)
scope("matcharm", s_matcharm)
scope("loopback", s_loopback)
scope("iflet", s_iflet)
scope("ifelse", s_ifelse)

SORDER = ["block", "earlyret", "break", "lblbreak", "continue", "divarm", "nestblk",
          "matcharm", "loopback", "iflet", "ifelse"]
E2 = "enum E2 { S(i64), N }"

for pn in PORDER:
    P = PAY[pn]
    for sn in SORDER:
        d, body, cnt, val, why = SCOPES[sn](P)
        extra = [E2] if sn == "iflet" else []
        emit("b_%s_%s" % (pn, sn), "B", dict(payload=pn, scope=sn), cnt, val, why,
             P['uses'], P['decls'] + extra + d, body)

# ------------------------------------------------------------- BLOCK C: the path an assignment goes through
# Assigning over a live place must drop what was there.  Vary the PATH to the place.
C = []


def cadd(cid, path, cnt, val, why, uses, decls, body):
    emit("c_" + cid, "C", dict(path=path), cnt, val, why, uses, decls, body)


cadd("assign_local", "direct", 1001, 1000,
     "assignment to a local drops the old value at the assignment; the new one at scope end",
     [], [], "    { let mut x: D = %s; x = %s; r = x.v; }\n" % (D(1), D(1000)))
cadd("assign_through_refmut", "&mut", 1001, 1000,
     "`*q = v` through a `&mut` drops the pointee that was there, then the new one at scope end",
     [], [], "    { let mut x: D = %s; let q: &mut D = &mut x; *q = %s; r = x.v; }\n" % (D(1), D(1000)))
cadd("assign_field", "field", 1001, 1000,
     "assigning a field drops the OLD field value only; the enclosing struct drops the new one at scope end",
     [], [W1], "    { let mut w: W1 = W1{d:%s, n:5i64}; w.d = %s; r = w.d.v; }\n" % (D(1), D(1000)))
cadd("assign_field_other_live", "field", 1011, 1000,
     "assigning ONE droppable field of two drops only that field; the sibling still drops at scope end",
     [], [W2], "    { let mut w: W2 = W2{d:%s, e:%s}; w.d = %s; r = w.d.v; }\n" % (D(1), D(10), D(1000)))
cadd("assign_index", "index", 1011, 1000,
     "assigning an array element drops the OLD element; the new one and the untouched sibling drop at scope end",
     [], [], "    { let mut a: [D; 2] = [%s, %s]; a[0] = %s; r = a[0].v; }\n" % (D(1), D(10), D(1000)))
cadd("assign_box_deref", "box", 1001, 1000,
     "`*b = v` through a Box drops the boxed value that was there; the new one dies with the Box",
     [BOXED], [], "    { let mut b: Box<D> = Box::new(%s); *b = %s; r = b.v; }\n" % (D(1), D(1000)))
cadd("assign_nested_field", "field.field", 1001, 1000,
     "a two-hop field path names one place: only the value at that place is dropped",
     [], [W1, OUTS], "    { let mut o: Out = Out{i: W1{d:%s, n:5i64}}; o.i.d = %s; r = o.i.d.v; }\n" % (D(1), D(1000)))
cadd("assign_tuple_elem", "tuple.0", 1011, 1000,
     "assigning one tuple element drops that element only; the sibling drops at scope end",
     [], [], "    { let mut t: (D, D) = (%s, %s); t.0 = %s; r = t.0.v; }\n" % (D(1), D(10), D(1000)))
cadd("assign_whole_struct", "direct", 11011, 1000,
     "assigning the WHOLE struct drops both old fields, then the two new ones at scope end",
     [], [W2], "    { let mut w: W2 = W2{d:%s, e:%s}; w = W2{d:%s, e:%s}; r = w.d.v; }\n"
     % (D(1), D(10), D(1000), D(10000)))
cadd("assign_index_var", "index[var]", 1011, 1000,
     "an index that is not a literal names the same place: the old element is still dropped",
     [], [], "    { let mut a: [D; 2] = [%s, %s]; let i: u64 = 0u64; a[i] = %s; r = a[0].v; }\n"
     % (D(1), D(10), D(1000)))
cadd("assign_field_through_refmut", "&mut .field", 1001, 1000,
     "a field assignment reached through a `&mut` to the struct drops the old field value",
     [], [W1], "    { let mut w: W1 = W1{d:%s, n:5i64}; let q: &mut W1 = &mut w; q.d = %s; r = w.d.v; }\n"
     % (D(1), D(1000)))
cadd("assign_self_move", "direct", 11, 1,
     "moving out of one local into another leaves the source with no value: exactly one whole drop",
     [], [W2], "    { let x: W2 = W2{d:%s, e:%s}; let y: W2 = x; r = y.d.v; }\n" % (D(1), D(10)))
cadd("swap_three", "direct", 11, 1,
     "a three-step swap moves each value exactly once: two owners, two drops",
     [], [], "    { let mut x: D = %s; let mut y: D = %s; let t: D = x; x = y; y = t; r = y.v; }\n"
     % (D(1), D(10)))
cadd("read_after_partial", "field", 11, 10,
     "after one field is moved out, the OTHER field is still readable and still drops",
     [], [W2, "fn eatd(x: D) -> i64 { return x.v; }"],
     "    { let w: W2 = W2{d:%s, e:%s}; let k: i64 = eatd(w.d); r = w.e.v + k - 1i64; }\n" % (D(1), D(10)))
cadd("move_out_of_box", "box", 1, 1,
     "moving the boxed value out of a Box (Box is the one type with DerefMove) leaves the Box empty: one drop",
     [BOXED], ["fn eatd(x: D) -> i64 { return x.v; }"],
     "    { let b: Box<D> = Box::new(%s); r = eatd(*b); }\n" % D(1))
cadd("move_field_out_of_box", "box.field", 1, 1,
     "moving a droppable FIELD out of a Box: the field drops in the callee, nothing else is droppable",
     [BOXED], [W1, "fn eatd(x: D) -> i64 { return x.v; }"],
     "    { let b: Box<W1> = Box::new(W1{d:%s, n:5i64}); r = eatd(b.d); }\n" % D(1))
cadd("temp_argument", "temp", 1, 1,
     "a temporary built directly as an argument is owned by the callee and dropped there, once",
     [], ["fn eatd(x: D) -> i64 { return x.v; }"], "    { r = eatd(%s); }\n" % D(1))
cadd("temp_unbound", "temp", 1, 42,
     "a temporary whose value is discarded still drops at the end of the enclosing statement",
     [], ["fn peek(x: &D) -> i64 { return x.v; }"], "    { let k: i64 = peek(&%s); r = 42i64 + k - 1i64; }\n" % D(1))
cadd("nested_temp_in_ctor", "temp", 11, 1,
     "a temporary consumed by a constructor is not dropped separately: it IS the field",
     [], [W2], "    { let w: W2 = W2{d:%s, e:%s}; r = w.d.v; }\n" % (D(1), D(10)))
cadd("vec_push_then_drop", "vec", 111, 1,
     "a Vec drops every element it holds: three pushes, three element drops",
     [], ["fn mk3(p: *mut i64) -> Vec<D> { let mut xs: Vec<D> = Vec::<D>::new(); xs.push(%s); xs.push(%s); xs.push(%s); return xs; }"
          % (D(1), D(10), D(100))],
     "    { let xs: Vec<D> = mk3(p); r = xs.borrow(0u64).v; }\n")
cadd("vec_reassign", "vec", 11011, 1000,
     "assigning over a Vec drops the old Vec AND every element it held",
     [], ["fn mk2(p: *mut i64, k: i64) -> Vec<D> { let mut xs: Vec<D> = Vec::<D>::new(); xs.push(D{v:k,c:p}); xs.push(D{v:k*10i64,c:p}); return xs; }"],
     "    { let mut xs: Vec<D> = mk2(p, 1i64); xs = mk2(p, 1000i64); r = xs.borrow(0u64).v; }\n")
cadd("array_of_struct", "index.field", 1111, 1,
     "an array of two-owner structs drops all four owners",
     [], [W2], "    { let a: [W2; 2] = [W2{d:%s, e:%s}, W2{d:%s, e:%s}]; r = a[0].d.v; }\n"
     % (D(1), D(10), D(100), D(1000)))
cadd("patbind_move_out", "pattern", 11, 1,
     "a destructuring `let` moves each element into its binder: each binder drops once, the source not at all",
     [], [W2], "    { let w: W2 = W2{d:%s, e:%s}; let W2{ d: dd, e: ee } = w; r = dd.v + ee.v - 10i64; }\n"
     % (D(1), D(10)))
cadd("patbind_one_moved", "pattern", 11, 1,
     "`W2{ d: dd, .. }` moves ONE field into a binder and leaves the other in the source: both still drop once",
     [], [W2], "    { let w: W2 = W2{d:%s, e:%s}; let W2{ d: dd, .. } = w; r = dd.v; }\n" % (D(1), D(10)))
cadd("patbind_ref", "pattern", 11, 1,
     "`ref` binders move nothing: the source keeps both owners and drops both at scope end",
     [], [W2], "    { let w: W2 = W2{d:%s, e:%s}; let W2{ d: ref dd, e: ref ee } = w; r = dd.v + ee.v - 10i64; }\n"
     % (D(1), D(10)))
cadd("match_move_payload", "pattern", 1, 1,
     "a match arm that moves the payload out of an enum: the payload drops once, the enum shell holds nothing",
     [], [ENUM], "    { let e: E = E::V(%s); match e { E::V(d) => { r = d.v; }, E::Z => {} } }\n" % D(1))
cadd("match_ref_payload", "pattern", 1, 1,
     "a match arm that binds the payload by `ref`: the enum still owns it and drops it at scope end",
     [], [ENUM], "    { let e: E = E::V(%s); match e { E::V(ref d) => { r = d.v; }, E::Z => {} } }\n" % D(1))

# ------------------------------------------------------------- BLOCK D: DROP ORDER
def oadd(cid, what, cnt, val, why, uses, decls, body):
    emit("d_" + cid, "D", dict(order=what), cnt, val, why, uses, decls, body, pre=PRE_O)


def O(w):
    return "O{v:%di64,c:p}" % w


oadd("locals_reverse", "locals", 321, 6,
     "locals are dropped in REVERSE declaration order: c, b, a -> 3, 2, 1",
     [], [], "    { let a: O = %s; let b: O = %s; let c: O = %s; r = a.v + b.v + c.v; }\n" % (O(1), O(2), O(3)))
oadd("struct_fields_declorder", "fields", 123, 1,
     "struct fields are dropped in DECLARATION order: a, b, c -> 1, 2, 3",
     [], ["struct T3 { a: O, b: O, c: O }"],
     "    { let w: T3 = T3{a:%s, b:%s, c:%s}; r = w.a.v; }\n" % (O(1), O(2), O(3)))
oadd("tuple_elems_order", "tuple", 123, 1,
     "tuple elements are dropped left to right: .0, .1, .2",
     [], [], "    { let t: (O, O, O) = (%s, %s, %s); r = t.0.v; }\n" % (O(1), O(2), O(3)))
oadd("array_elems_order", "array", 123, 1,
     "array elements are dropped in index order: 0, 1, 2",
     [], [], "    { let a: [O; 3] = [%s, %s, %s]; r = a[0].v; }\n" % (O(1), O(2), O(3)))
oadd("nested_outer_then_inner", "nested", 12, 1,
     "a struct's own Drop::drop runs BEFORE its fields' -- here the outer struct has no Drop, so the field order is all there is",
     [], ["struct Wo { a: O, b: O }"],
     "    { let w: Wo = Wo{a:%s, b:%s}; r = w.a.v; }\n" % (O(1), O(2)))
oadd("locals_and_temps", "temps", 21, 2,
     "a temporary argument dies at the end of its statement, BEFORE the local declared before it",
     [], ["fn peek(x: &O) -> i64 { return x.v; }"],
     "    { let a: O = %s; let k: i64 = peek(&%s); r = k; }\n" % (O(1), O(2)))
oadd("loop_iterations_order", "loop", 123, 3,
     "each iteration's local dies at the end of that iteration, so the order is the iteration order",
     [], [], "    { let mut i: i64 = 1i64; while i < 4i64 { let x: O = O{v:i,c:p}; r = x.v; i = i + 1i64; } }\n")
oadd("moved_out_order", "move", 231, 2,
     "b is moved into the callee and dies there FIRST (2); then the two survivors die in reverse declaration order, c then a (3, 1)",
     [], ["fn eato(x: O) -> i64 { return x.v; }"],
     "    { let a: O = %s; let b: O = %s; let c: O = %s; let k: i64 = eato(b); r = a.v + c.v + k - 4i64; }\n"
     % (O(1), O(2), O(3)))
oadd("vec_elem_order", "vec", 123, 1,
     "a Vec drops its elements front to back",
     [], ["fn mk3(p: *mut i64) -> Vec<O> { let mut xs: Vec<O> = Vec::<O>::new(); xs.push(%s); xs.push(%s); xs.push(%s); return xs; }"
          % (O(1), O(2), O(3))],
     "    { let xs: Vec<O> = mk3(p); r = xs.borrow(0u64).v; }\n")
oadd("assign_drops_before_new", "assign", 12, 2,
     "the old value dies at the assignment, so it dies BEFORE the value that replaced it",
     [], [], "    { let mut x: O = %s; x = %s; r = x.v; }\n" % (O(1), O(2)))
oadd("nested_block_first", "nested", 21, 1,
     "the inner block's local dies at the inner brace, before the outer block's local",
     [], [], "    { let a: O = %s; { let b: O = %s; r = b.v; } r = a.v; }\n" % (O(1), O(2)))
oadd("struct_with_drop_then_fields", "dropimpl", 312, 1,
     "a type with its own Drop::drop runs the impl FIRST (3), then its fields in declaration order (1, 2)",
     [], ["struct Wd { a: O, b: O, c: *mut i64 }",
          "impl Drop for Wd { fn drop(self: &mut Wd) { unsafe { *self.c = *self.c * 10i64 + 3i64; } } }"],
     "    { let w: Wd = Wd{a:%s, b:%s, c:p}; r = w.a.v; }\n" % (O(1), O(2)))

# ------------------------------------------------------------- BLOCK E: shapes the product misses
def eadd(cid, what, cnt, val, why, uses, decls, body, pre=PRE_D):
    emit("e_" + cid, "E", dict(shape=what), cnt, val, why, uses, decls, body, pre=pre)


PEEKO = "fn peeko(x: &O) -> i64 { return x.v; }"
PEEKD = "fn peekd(x: &D) -> i64 { return x.v; }"
EATD = "fn eatd(x: D) -> i64 { return x.v; }"

eadd("let_underscore_drops_now", "let _", 12, 2,
     "`let _ = e` binds nothing: e is dropped AT THAT STATEMENT (1), before the later local (2)",
     [], [], "    { let _ = %s; let a: O = %s; r = a.v; }\n" % (O(1), O(2)), pre=PRE_O)
eadd("let_underscore_named_lives", "let _x", 21, 2,
     "`let _x = e` IS a binding: it lives to the end of the block, so it dies after the later local (2, then 1)",
     [], [], "    { let _x: O = %s; let a: O = %s; r = a.v; }\n" % (O(1), O(2)), pre=PRE_O)
eadd("temp_in_if_condition", "temp", 21, 1,
     "a temporary in an `if` condition dies at the end of the condition, before the block runs and before the outer local",
     [], [PEEKO], "    { let a: O = %s; if peeko(&%s) > 0i64 { r = a.v; } }\n" % (O(1), O(2)), pre=PRE_O)
eadd("temp_in_while_condition", "temp", 221, 1,
     "a temporary in a `while` condition is built and dropped on every test of the condition -- twice here, then the local",
     [], [PEEKO], "    { let a: O = %s; let mut i: i64 = 0i64;\n"
                  "      while peeko(&%s) > 0i64 && i < 1i64 { i = i + 1i64; }\n      r = a.v; }\n" % (O(1), O(2)), pre=PRE_O)
eadd("method_self_by_value", "method", 1, 1,
     "a method taking `self` by value owns it: it drops in the method, once",
     [], ["impl D { fn take(self: D) -> i64 { return self.v; } }"],
     "    { let x: D = %s; r = x.take(); }\n" % D(1))
eadd("generic_fn_consumes", "generic", 1, 42,
     "a generic fn taking T by value must run T's drop glue at its own scope end",
     [], ["fn consume<T>(t: T) -> i64 { return 42i64; }"],
     "    { let x: D = %s; r = consume::<D>(x); }\n" % D(1))
eadd("generic_fn_consumes_copy", "generic", 0, 42,
     "the same generic fn at T = i64 has no drop glue at all: zero drops",
     [], ["fn consume2<T>(t: T) -> i64 { return 42i64; }"],
     "    { let x: i64 = 5i64; r = consume2::<i64>(x); }\n")
eadd("trait_default_consumes", "trait", 1, 1,
     "a trait DEFAULT body taking `self` by value drops it at the default body's scope end",
     [], ["trait Take { fn take(self: Self) -> i64 { return 0i64; } }",
          "impl Take for D { fn take(self: D) -> i64 { return self.v; } }"],
     "    { let x: D = %s; r = x.take(); }\n" % D(1))
eadd("for_over_vec", "for", 111, 1,
     "a `for` over a Vec consumes it and drops each of the three items exactly once",
     [], ["fn mk3(p: *mut i64) -> Vec<D> { let mut xs: Vec<D> = Vec::<D>::new(); xs.push(%s); xs.push(%s); xs.push(%s); return xs; }" % (D(1), D(10), D(100))],
     "    { let xs: Vec<D> = mk3(p); let mut k: i64 = 0i64;\n"
     "      for y in xs { k = k + y.v; }\n      r = k - 110i64; }\n")
eadd("boxbox", "box", 1, 1,
     "a Box of a Box drops the inner value exactly once",
     [BOXED], [], "    { let b: Box<Box<D>> = Box::new(Box::new(%s)); r = (*b).v; }\n" % D(1))
eadd("vec_of_box", "vec", 11, 1,
     "a Vec of Boxes drops every boxed value once",
     [BOXED], ["fn mkvb(p: *mut i64) -> Vec<Box<D>> { let mut xs: Vec<Box<D>> = Vec::<Box<D>>::new(); xs.push(Box::new(%s)); xs.push(Box::new(%s)); return xs; }" % (D(1), D(10))],
     "    { let xs: Vec<Box<D>> = mkvb(p); r = (*xs.borrow(0u64)).v; }\n")
eadd("array_of_box", "array", 11, 1,
     "an array of Boxes drops every boxed value once",
     [BOXED], [], "    { let a: [Box<D>; 2] = [Box::new(%s), Box::new(%s)]; r = a[0].v; }\n" % (D(1), D(10)))
eadd("vec_of_vec", "vec", 1111, 1,
     "a Vec of Vecs drops every element of every inner Vec",
     [], ["fn mk2(p: *mut i64, k: i64) -> Vec<D> { let mut xs: Vec<D> = Vec::<D>::new(); xs.push(D{v:k,c:p}); xs.push(D{v:k*10i64,c:p}); return xs; }",
          "fn mkvv(p: *mut i64) -> Vec<Vec<D>> { let mut ys: Vec<Vec<D>> = Vec::<Vec<D>>::new(); ys.push(mk2(p,1i64)); ys.push(mk2(p,100i64)); return ys; }"],
     "    { let ys: Vec<Vec<D>> = mkvv(p); r = ys.borrow(0u64).borrow(0u64).v; }\n")
eadd("reinit_after_move", "reinit", 1001, 1000,
     "a local moved out and then RE-ASSIGNED is live again: the moved value drops in the callee, the new one at scope end",
     [], [EATD], "    { let mut x: D = %s; let k: i64 = eatd(x); x = %s; r = x.v + k - 1i64; }\n" % (D(1), D(1000)))
eadd("reinit_field_after_move", "reinit", 1011, 1000,
     "a FIELD moved out and then re-assigned: the moved field drops in the callee (1), and the new field (1000) and the sibling (10) at scope end -- the assignment must NOT drop the moved-out place again",
     [], [W2, EATD],
     "    { let mut w: W2 = W2{d:%s, e:%s}; let k: i64 = eatd(w.d); w.d = %s; r = w.d.v + k - 1i64; }\n"
     % (D(1), D(10), D(1000)))
eadd("both_fields_moved", "partial", 11, 1,
     "BOTH droppable fields moved out one at a time: each drops in its callee and the shell drops nothing",
     [], [W2, EATD],
     "    { let w: W2 = W2{d:%s, e:%s}; let k: i64 = eatd(w.d); let m: i64 = eatd(w.e); r = k + m - 10i64; }\n"
     % (D(1), D(10)))
eadd("partial_then_early_return", "partial", 11, 1,
     "one field moved out and then an early return: the moved one drops in the callee, the survivor on the return path",
     [], [W2, EATD,
          "fn f(p: *mut i64, k: i64) -> i64 { let w: W2 = W2{d:%s, e:%s}; let m: i64 = eatd(w.d); if k > 0i64 { return m; } return 0i64; }" % (D(1), D(10))],
     "    { r = f(p, 1i64); }\n")
eadd("move_into_struct_literal", "ctor", 11, 1,
     "a local moved into a struct literal is owned by the struct: one drop each, none at the source",
     [], [W2], "    { let a: D = %s; let b: D = %s; let w: W2 = W2{d:a, e:b}; r = w.d.v; }\n" % (D(1), D(10)))
eadd("move_into_vec_push", "vec", 11, 1,
     "locals moved into a Vec by push are owned by the Vec: two drops when the Vec dies",
     [], [], "    { let a: D = %s; let b: D = %s; let mut xs: Vec<D> = Vec::<D>::new(); xs.push(a); xs.push(b); r = xs.borrow(0u64).v; }\n"
     % (D(1), D(10)))
eadd("closure_boxed_dyn", "closure", 1, 1,
     "a `move` closure stored behind Box<dyn Fn> owns its capture and drops it when the box dies",
     [BOXED], ["trait Fn0 { fn call(self: &Self) -> i64; }"],
     "    { let x: D = %s;\n      let f = move || -> i64 { return x.v; };\n      let g: Box<dyn Fn() -> i64> = Box::new(f) as Box<dyn Fn() -> i64>;\n      r = g(); }\n" % D(1))
eadd("closure_by_ref_capture", "closure", 1, 1,
     "a non-`move` closure borrows its capture: the OWNER still drops it, once",
     [], [], "    { let x: D = %s;\n      let f = || -> i64 { return x.v; };\n      r = f(); }\n" % D(1))
eadd("closure_called_twice", "closure", 1, 1,
     "a `move` closure called twice still owns ONE capture and drops it once",
     [], [], "    { let x: D = %s;\n      let f = move || -> i64 { return x.v; };\n      let k: i64 = f(); r = f() + k - 1i64; }\n" % D(1))
eadd("zero_sized_drop", "zst", 0, 1,
     "a field-less struct with a Drop impl still has its destructor run once; the count here is kept in a static because the value carries no pointer",
     [], ["struct Z { }", "static mut ZH: i64 = 0i64;",
          "impl Drop for Z { fn drop(self: &mut Z) { unsafe { ZH = ZH + 1i64; } } }"],
     "    { let z: Z = Z{}; }\n    r = unsafe { ZH };\n")
eadd("match_scrutinee_temp", "temp", 1, 1,
     "a temporary built as the match scrutinee lives for the whole match and dies at its end, once",
     [], ["fn mkd(p: *mut i64) -> D { return %s; }" % D(1)],
     "    { match mkd(p) { d => { r = d.v; } } }\n")
eadd("match_arm_moves_scrutinee_local", "match", 1, 1,
     "a match arm that moves the scrutinee local out: one drop, in the arm's callee",
     [], [EATD], "    { let x: D = %s; match 0i64 { 0i64 => { r = eatd(x); }, _ => {} } }\n" % D(1))
eadd("match_one_arm_moves", "match", 1, 1,
     "the arm that moves is NOT taken, so the local is still live at the end of the match and drops there",
     [], [EATD], "    { let x: D = %s; match 1i64 { 0i64 => { r = eatd(x); }, _ => { r = x.v; } } }\n" % D(1))
eadd("while_let_body_local", "whilelet", 11, 1,
     "a `while let` body is a scope: the local built inside it drops on each of the two iterations",
     [], ["enum Cnt { S(i64), N }",
          "fn step(i: i64) -> Cnt { if i < 2i64 { return Cnt::S(i); } return Cnt::N; }"],
     "    { let mut i: i64 = 0i64; let mut k: i64 = 1i64;\n"
     "      while let Cnt::S(j) = step(i) { let x: D = D{v:k,c:p}; r = x.v; k = k * 10i64; i = i + 1i64; }\n"
     "      r = 1i64; }\n")
eadd("nested_fn_returns_field", "return", 1, 1,
     "a fn that builds a struct and returns ONE field: the returned field drops in the caller, nothing else is droppable",
     [], [W1, "fn takefield(p: *mut i64) -> D { let w: W1 = W1{d:%s, n:5i64}; return w.d; }" % D(1)],
     "    { let d: D = takefield(p); r = d.v; }\n")
eadd("struct_update_rest", "fru", 1011, 1000,
     "functional-update `W2{ d: new, ..old }` moves old's e into the new struct; old's d is NOT moved and dies with old (1), the new d (1000) and the moved e (10) die with the new struct",
     [], [W2], "    { let o: W2 = W2{d:%s, e:%s}; let w: W2 = W2{ d: %s, ..o }; r = w.d.v; }\n" % (D(1), D(10), D(1000)))
eadd("array_elem_move_refused", "index", 0, 0,
     "moving out of an array INDEX is illegal in Rust (E0507): this cell must be REFUSED",
     [], [EATD], "    { let a: [D; 2] = [%s, %s]; r = eatd(a[0]); }\n" % (D(1), D(10)))
eadd("vec_elem_move_refused", "index", 0, 0,
     "moving out of a Vec index is illegal in Rust (E0507): this cell must be REFUSED",
     [], [EATD], "    { let mut xs: Vec<D> = Vec::<D>::new(); xs.push(%s); r = eatd(xs.borrow(0u64)); }\n" % D(1))
eadd("use_after_move_refused", "moved", 0, 0,
     "reading a local after it was moved is illegal in Rust (E0382): this cell must be REFUSED",
     [], [EATD], "    { let x: D = %s; let k: i64 = eatd(x); r = x.v + k; }\n" % D(1))
eadd("double_move_refused", "moved", 0, 0,
     "moving the same local twice is illegal in Rust (E0382): this cell must be REFUSED",
     [], [EATD], "    { let x: D = %s; let k: i64 = eatd(x); r = eatd(x) + k; }\n" % D(1))
eadd("move_in_loop_refused", "loop", 0, 0,
     "a move in a loop body reachable on a back edge is illegal in Rust (E0382): this cell must be REFUSED",
     [], [EATD], "    { let x: D = %s; let mut i: i64 = 0i64; while i < 3i64 { r = eatd(x); i = i + 1i64; } }\n" % D(1))
eadd("partial_then_whole_move_refused", "partial", 0, 0,
     "moving the whole struct after one field was moved out is illegal in Rust (E0382): this cell must be REFUSED",
     [], [W2, EATD, "fn eatw(w: W2) -> i64 { return w.e.v; }"],
     "    { let w: W2 = W2{d:%s, e:%s}; let k: i64 = eatd(w.d); r = eatw(w) + k; }\n" % (D(1), D(10)))
eadd("generic_struct_drop_impl", "generic", 3, 5,
     "a Drop impl on a GENERIC struct runs at T = D: the impl adds 2 and the field's own drop adds 1",
     [], ["struct Gd<T> { t: T, c: *mut i64 }",
          "impl<T> Drop for Gd<T> { fn drop(self: &mut Gd<T>) { unsafe { *self.c = *self.c + 2i64; } } }"],
     "    { let g: Gd<D> = Gd::<D> { t: %s, c: p }; r = 5i64; }\n" % D(1))
eadd("two_type_params_one_droppable", "generic", 1, 1,
     "a two-parameter generic with only one droppable argument drops exactly that one",
     [], ["struct G2<A, B> { a: A, b: B }"],
     "    { let g: G2<D, i64> = G2::<D, i64> { a: %s, b: 5i64 }; r = g.a.v; }\n" % D(1))
eadd("tuple_struct_payload", "tuplestruct", 11, 1,
     "a tuple struct with two droppable fields drops both",
     [], ["struct TS(D, D);"], "    { let t: TS = TS(%s, %s); r = t.0.v; }\n" % (D(1), D(10)))
eadd("enum_two_payload_read", "enum", 11, 1,
     "an enum variant with two droppable payload fields, read only: both drop with the enum",
     [], ["enum E3 { V(D, D), Z }"],
     "    { let e: E3 = E3::V(%s, %s); match e { E3::V(ref a, ref b) => { r = a.v + b.v - 10i64; }, E3::Z => {} } }\n"
     % (D(1), D(10)))
eadd("enum_two_payload_move_both", "enum", 11, 1,
     "both payload fields moved out by the arm: each drops once in the arm, the shell drops nothing",
     [], ["enum E3b { V(D, D), Z }"],
     "    { let e: E3b = E3b::V(%s, %s); match e { E3b::V(a, b) => { r = a.v + b.v - 10i64; }, E3b::Z => {} } }\n"
     % (D(1), D(10)))
eadd("enum_other_variant", "enum", 0, 7,
     "a variant carrying no payload has nothing to drop",
     [], ["enum E4 { V(D), Z }"],
     "    { let e: E4 = E4::Z; match e { E4::V(ref d) => { r = d.v; }, E4::Z => { r = 7i64; } } }\n")
eadd("self_assign", "assign", 1, 1,
     "`x = x` moves a value onto itself: it must still be dropped exactly once",
     [], [], "    { let mut x: D = %s; x = x; r = x.v; }\n" % D(1))
eadd("drop_order_across_return", "order", 21, 1,
     "an inner block's local dies at the inner brace even when the outer fn returns right after",
     [], [PEEKO, "fn f(p: *mut i64) -> i64 { let a: O = %s; { let b: O = %s; let k: i64 = peeko(&b); if k > 0i64 { return a.v; } } return 0i64; }" % (O(1), O(2))],
     "    { r = f(p); }\n", pre=PRE_O)
eadd("cond_move_both_branches", "branch", 1, 1,
     "moved in BOTH branches: exactly one drop whichever branch runs, and none at scope end",
     [], [EATD], "    { let x: D = %s; if r < 0i64 { r = eatd(x); } else { r = eatd(x); } }\n" % D(1))
eadd("cond_move_then_read_refused", "branch", 0, 0,
     "reading after a move that happened on only ONE path is illegal in Rust (E0382): this cell must be REFUSED",
     [], [EATD], "    { let x: D = %s; if r < 0i64 { let k: i64 = eatd(x); r = k; } r = x.v; }\n" % D(1))
eadd("recursive_box_list", "box", 111, 1,
     "a Box-linked list drops every node's payload exactly once",
     [BOXED], ["struct Node { d: D, next: Box<NodeOpt> }", "enum NodeOpt { Cons(Node), Nil }",
               "fn mklist(p: *mut i64) -> Node { return Node{ d: %s, next: Box::new(NodeOpt::Cons(Node{ d: %s, next: Box::new(NodeOpt::Cons(Node{ d: %s, next: Box::new(NodeOpt::Nil) })) })) }; }" % (D(1), D(10), D(100))],
     "    { let l: Node = mklist(p); r = l.d.v; }\n")


# ------------------------------------------------------------- BLOCK F: the roots the first sweep named, probed at their edges
def fadd(cid, what, cnt, val, why, uses, decls, body, pre=PRE_D):
    emit("f_" + cid, "F", dict(edge=what), cnt, val, why, uses, decls, body, pre=pre)


fadd("shadow_three", "shadow", 1011, 1000,
     "three bindings of ONE name in ONE frame: all three values die at the end of the block",
     [], [], "    { let x: D = %s; let x: D = %s; let x: D = %s; r = x.v; }\n" % (D(1), D(10), D(1000)))
fadd("shadow_nested_block_ctl", "shadow", 1001, 1000,
     "CONTROL: the same two bindings in NESTED frames -- both die, the inner one first",
     [], [], "    { let x: D = %s; { let x: D = %s; r = x.v; } }\n" % (D(1), D(1000)))
fadd("shadow_distinct_names_ctl", "shadow", 1001, 1000,
     "CONTROL: two bindings of DIFFERENT names in one frame -- both die",
     [], [], "    { let x: D = %s; let y: D = %s; r = y.v; }\n" % (D(1), D(1000)))
fadd("shadow_mut_first", "shadow", 1001, 1000,
     "the shadowed binding being `mut` changes nothing: both values still die",
     [], [], "    { let mut x: D = %s; let x: D = %s; r = x.v; }\n" % (D(1), D(1000)))
fadd("shadow_after_move", "shadow", 1001, 1000,
     "the first binding is MOVED OUT before being shadowed: it dies in the callee, the second at scope end",
     [], [EATD], "    { let x: D = %s; let k: i64 = eatd(x); let x: D = %s; r = x.v + k - 1i64; }\n" % (D(1), D(1000)))
fadd("shadow_fn_param", "shadow", 1001, 1000,
     "a parameter shadowed by a local of the same name: the argument still dies with the frame",
     [], ["fn g(x: D, p: *mut i64) -> i64 { let x: D = %s; return x.v; }" % D(1000)],
     "    { r = g(%s, p); }\n" % D(1))
fadd("shadow_loop_body", "shadow", 22, 1000,
     "shadowing inside a loop body: on each of two iterations BOTH values die at the end of the body -- 2 * (1 + 10)",
     [], [], "    { let mut i: i64 = 0i64;\n"
             "      while i < 2i64 { let x: D = D{v:1i64,c:p}; let x: D = D{v:10i64,c:p}; r = x.v; i = i + 1i64; }\n"
             "      r = 1000i64; }\n")
fadd("assign_field_of_indexed", "assign-place", 1015, 1000,
     "a field path ROOTED AT AN INDEX names a place like any other: `a[0].d = new` must drop the old field",
     [], [W2], "    { let mut a: [W2; 2] = [W2{d:%s, e:%s}, W2{d:%s, e:%s}];\n"
               "      a[0].d = %s; r = a[0].d.v; }\n" % (D(1), D(2), D(4), D(8), D(1000)))
fadd("assign_indexed_of_field", "assign-place", 1013, 1000,
     "an index path rooted at a FIELD is still a place: `w.a[0] = new` must drop the old element",
     [], ["struct Wa { a: [D; 2], n: i64 }"],
     "    { let mut w: Wa = Wa{a:[%s, %s], n:5i64}; w.a[0] = %s; r = w.a[0].v; }\n" % (D(1), D(2), D(1000)))
fadd("assign_tuple_in_struct", "assign-place", 1013, 1000,
     "a tuple-index path rooted at a field: `w.t.0 = new` must drop the old element",
     [], ["struct Wt { t: (D, D), n: i64 }"],
     "    { let mut w: Wt = Wt{t:(%s, %s), n:5i64}; w.t.0 = %s; r = w.t.0.v; }\n" % (D(1), D(2), D(1000)))
fadd("assign_index_through_refmut", "assign-place", 1011, 1000,
     "an element assignment reached through a `&mut` to the array must drop the old element",
     [], [], "    { let mut a: [D; 2] = [%s, %s]; let q: &mut [D; 2] = &mut a; q[0] = %s; r = a[0].v; }\n"
     % (D(1), D(10), D(1000)))
fadd("assign_deref_field_ctl", "assign-place", 1001, 1000,
     "CONTROL for the same predicate at the DEREF place kind: `(*q).d = new` drops the old field",
     [], [W1], "    { let mut w: W1 = W1{d:%s, n:5i64}; let q: &mut W1 = &mut w; (*q).d = %s; r = w.d.v; }\n"
     % (D(1), D(1000)))
fadd("box_deref_move_in_arg", "box-move", 1, 1,
     "moving the boxed value out of a Box in an ARGUMENT position is the same move the `let` position already lowers",
     [BOXED], [EATD], "    { let b: Box<D> = Box::new(%s); r = eatd(*b); }\n" % D(1))
fadd("box_deref_move_in_let_ctl", "box-move", 1, 1,
     "CONTROL: the same move at the `let` position, which `try_lower_box_deref_move` already handles",
     [BOXED], [EATD], "    { let b: Box<D> = Box::new(%s); let d: D = *b; r = eatd(d); }\n" % D(1))
fadd("box_deref_move_in_return", "box-move", 1, 1,
     "the same move in a RETURN position",
     [BOXED], ["fn takeout(p: *mut i64) -> D { let b: Box<D> = Box::new(%s); return *b; }" % D(1)],
     "    { let d: D = takeout(p); r = d.v; }\n")
fadd("box_deref_move_in_ctor", "box-move", 1, 1,
     "the same move into a struct literal field",
     [BOXED], [W1], "    { let b: Box<D> = Box::new(%s); let w: W1 = W1{ d: *b, n: 5i64 }; r = w.d.v; }\n" % D(1))
fadd("let_underscore_after", "let _", 21, 1,
     "`let _ = e` after a local: e dies at ITS statement (2), the local at the block end (1)",
     [], [], "    { let a: O = %s; let _ = %s; r = a.v; }\n" % (O(1), O(2)), pre=PRE_O)
fadd("let_underscore_call", "let _", 12, 2,
     "`let _ = f()` discards a returned owner at the statement, before the later local",
     [], ["fn mko(p: *mut i64) -> O { return %s; }" % O(1)],
     "    { let _ = mko(p); let a: O = %s; r = a.v; }\n" % O(2), pre=PRE_O)
fadd("expr_stmt_temp_dies_now", "temp", 12, 2,
     "CONTROL: a bare expression statement's value dies at that statement, before the later local",
     [], ["fn mko(p: *mut i64) -> O { return %s; }" % O(1)],
     "    { mko(p); let a: O = %s; r = a.v; }\n" % O(2), pre=PRE_O)
fadd("field_order_two", "order", 12, 1,
     "two fields, declaration order: a then b",
     [], ["struct F2 { a: O, b: O }"], "    { let w: F2 = F2{a:%s, b:%s}; r = w.a.v; }\n" % (O(1), O(2)), pre=PRE_O)
fadd("field_order_four", "order", 1234, 1,
     "four fields, declaration order",
     [], ["struct F4 { a: O, b: O, c: O, d: O }"],
     "    { let w: F4 = F4{a:%s, b:%s, c:%s, d:%s}; r = w.a.v; }\n" % (O(1), O(2), O(3), O(4)), pre=PRE_O)
fadd("tuple_order_two", "order", 12, 1,
     "two tuple elements, left to right",
     [], [], "    { let t: (O, O) = (%s, %s); r = t.0.v; }\n" % (O(1), O(2)), pre=PRE_O)
fadd("array_order_ctl", "order", 12, 1,
     "CONTROL: array elements already go in index order -- the sibling branch of the same walk",
     [], [], "    { let a: [O; 2] = [%s, %s]; r = a[0].v; }\n" % (O(1), O(2)), pre=PRE_O)
fadd("enum_payload_order", "order", 12, 1,
     "an enum variant's payload fields go in declaration order",
     [], ["enum Ep { V(O, O), Z }"],
     "    { let e: Ep = Ep::V(%s, %s); match e { Ep::V(ref a, ref b) => { r = a.v; }, Ep::Z => {} } }\n"
     % (O(1), O(2)), pre=PRE_O)
fadd("nested_struct_order", "order", 123, 1,
     "a struct whose FIRST field is itself a struct: the inner pair goes before the outer's later field",
     [], ["struct In2 { a: O, b: O }", "struct Ou2 { i: In2, c: O }"],
     "    { let w: Ou2 = Ou2{i: In2{a:%s, b:%s}, c:%s}; r = w.i.a.v; }\n" % (O(1), O(2), O(3)), pre=PRE_O)
fadd("param_drop_order", "order", 21, 1,
     "function parameters are dropped in REVERSE declaration order, like locals",
     [], ["fn two(a: O, b: O) -> i64 { return a.v; }"],
     "    { r = two(%s, %s); }\n" % (O(1), O(2)), pre=PRE_O)
fadd("vec_push_order", "order", 12, 1,
     "CONTROL: a Vec drops front to back",
     [], ["fn mk2o(p: *mut i64) -> Vec<O> { let mut xs: Vec<O> = Vec::<O>::new(); xs.push(%s); xs.push(%s); return xs; }" % (O(1), O(2))],
     "    { let xs: Vec<O> = mk2o(p); r = xs.borrow(0u64).v; }\n", pre=PRE_O)
fadd("compound_assign_no_drop", "assign", 1, 1,
     "a compound assignment on a plain i64 field of an owner drops nothing extra",
     [], [W1], "    { let mut w: W1 = W1{d:%s, n:5i64}; w.n = w.n + 1i64; r = w.d.v; }\n" % D(1))
fadd("for_over_array", "for", 11, 1,
     "a `for` over an array by value consumes it and drops each element once",
     [], [], "    { let a: [D; 2] = [%s, %s]; let mut k: i64 = 0i64; for y in a { k = k + y.v; } r = k - 10i64; }\n"
     % (D(1), D(10)))
fadd("drop_impl_moves_field", "dropimpl", 12, 1,
     "a Drop impl body that reads a field runs BEFORE the fields' own drops",
     [], ["struct Wm { a: O, c: *mut i64 }",
          "impl Drop for Wm { fn drop(self: &mut Wm) { unsafe { *self.c = *self.c * 10i64 + 1i64; } } }"],
     "    { let w: Wm = Wm{a:%s, c:p}; r = 1i64; }\n" % O(2), pre=PRE_O)
fadd("two_owners_one_moved_early_return", "branch", 11, 1,
     "one of two owners moved out, then an early return: exactly one drop each",
     [], [EATD, "fn f(p: *mut i64) -> i64 { let a: D = %s; let b: D = %s; let k: i64 = eatd(a); if k > 0i64 { return b.v; } return 0i64; }" % (D(1), D(10))],
     "    { r = f(p) - 9i64; }\n")
fadd("owner_moved_into_returned_struct", "return", 11, 1,
     "two owners moved into a struct that is then returned: two drops, in the caller",
     [], [W2, "fn mkw(p: *mut i64) -> W2 { let a: D = %s; let b: D = %s; return W2{d:a, e:b}; }" % (D(1), D(10))],
     "    { let w: W2 = mkw(p); r = w.d.v; }\n")
fadd("closure_fnonce_consumes", "closure", 1, 1,
     "a `move` closure whose body MOVES the capture into a callee: one drop, in that callee",
     [], [EATD], "    { let x: D = %s; let f = move || -> i64 { return eatd(x); }; r = f(); }\n" % D(1))
fadd("nested_closure_capture", "closure", 1, 1,
     "a capture carried through TWO closure layers is still dropped exactly once",
     [], [], "    { let x: D = %s;\n      let f = move || -> i64 { let g = move || -> i64 { return x.v; }; return g(); };\n      r = f(); }\n" % D(1))
fadd("match_mutref_scrutinee", "match", 1, 1,
     "matching a `&mut` to an owner binds by reference and moves nothing: one drop at the owner's scope end",
     [], [ENUM], "    { let mut e: E = E::V(%s); match &mut e { E::V(d) => { r = d.v; }, E::Z => {} } }\n" % D(1))
fadd("generic_vec_of_generic", "generic", 11, 1,
     "a Vec of a generic struct at T = D drops each instance's payload once",
     [], [GEN, "fn mkvg(p: *mut i64) -> Vec<G<D>> { let mut xs: Vec<G<D>> = Vec::<G<D>>::new(); xs.push(G::<D>{t:%s}); xs.push(G::<D>{t:%s}); return xs; }" % (D(1), D(10))],
     "    { let xs: Vec<G<D>> = mkvg(p); r = xs.borrow(0u64).t.v; }\n")
fadd("owner_in_static_slot", "assign", 1001, 1000,
     "a local declared uninitialised and assigned twice drops the first value at the second assignment",
     [], [], "    { let mut x: D; x = %s; x = %s; r = x.v; }\n" % (D(1), D(1000)))
fadd("owner_assigned_once_after_decl", "assign", 1, 1,
     "CONTROL: a local declared uninitialised and assigned ONCE drops nothing at the assignment",
     [], [], "    { let mut x: D; x = %s; r = x.v; }\n" % D(1))


# --------------------------------------------------- BLOCK G: the write itself, with no drop in the picture
# f_assign_index_through_refmut lost BOTH the store and the drop.  These cells take
# the destructor out so the two are not confounded: the oracle is the stored VALUE.
def gadd(cid, what, cnt, val, why, uses, decls, body):
    emit("g_" + cid, "G", dict(write=what), cnt, val, why, uses, decls, body,
         pre="""extern fn printf(fmt: *const u8, ...) -> i32;
fn emit(c: i64, v: i64) { unsafe { printf("count=%ld value=%ld\\n".as_ptr(), c, v); } }""")


gadd("idxwrite_local_refmut", "index", 77, 2,
     "`q[0] = v` through a LOCAL `&mut [i64; 2]` must store into the referent",
     [], [], "    let mut a: [i64; 2] = [1i64, 2i64];\n    let q: &mut [i64; 2] = &mut a;\n"
             "    q[0] = 77i64;\n    n = a[0]; r = a[1];\n")
gadd("idxwrite_local_refmut_explicit", "index", 77, 2,
     "CONTROL: the same store written with an explicit deref, `(*q)[0] = v`",
     [], [], "    let mut a: [i64; 2] = [1i64, 2i64];\n    let q: &mut [i64; 2] = &mut a;\n"
             "    (*q)[0] = 77i64;\n    n = a[0]; r = a[1];\n")
gadd("idxwrite_param_refmut", "index", 77, 2,
     "CONTROL: the same store through a `&mut [i64; 2]` PARAMETER",
     [], ["fn setit(q: &mut [i64; 2]) { q[0] = 77i64; }"],
     "    let mut a: [i64; 2] = [1i64, 2i64];\n    setit(&mut a);\n    n = a[0]; r = a[1];\n")
gadd("idxwrite_direct", "index", 77, 2,
     "CONTROL: the same store straight to the local array",
     [], [], "    let mut a: [i64; 2] = [1i64, 2i64];\n    a[0] = 77i64;\n    n = a[0]; r = a[1];\n")
gadd("idxwrite_struct_elem", "index", 77, 2,
     "the same store through a local `&mut` where the element is a struct",
     [], ["struct Sv { a: i64 }"],
     "    let mut a: [Sv; 2] = [Sv{a:1i64}, Sv{a:2i64}];\n    let q: &mut [Sv; 2] = &mut a;\n"
     "    q[0] = Sv{a:77i64};\n    n = a[0].a; r = a[1].a;\n")
gadd("idxread_local_refmut", "index", 1, 2,
     "CONTROL: READING `q[0]` through the same local `&mut` binding",
     [], [], "    let mut a: [i64; 2] = [1i64, 2i64];\n    let q: &mut [i64; 2] = &mut a;\n"
             "    n = q[0]; r = q[1];\n")
gadd("fieldwrite_local_refmut", "field", 77, 2,
     "CONTROL for the same sugar at a FIELD place: `q.a = v` through a local `&mut`",
     [], ["struct Sw { a: i64, b: i64 }"],
     "    let mut w: Sw = Sw{a:1i64, b:2i64};\n    let q: &mut Sw = &mut w;\n"
     "    q.a = 77i64;\n    n = w.a; r = w.b;\n")
gadd("idxwrite_vec_local_refmut", "index", 77, 2,
     "CONTROL: a Vec reached through a local `&mut`, written by its own method",
     [], [], "    let mut v: Vec<i64> = Vec::<i64>::new();\n    v.push(1i64); v.push(2i64);\n"
             "    let q: &mut Vec<i64> = &mut v;\n    q.set(0u64, 77i64);\n"
             "    n = *v.borrow(0u64); r = *v.borrow(1u64);\n")
gadd("idxwrite_local_ref_to_array_in_struct", "index", 77, 2,
     "a local `&mut` to an ARRAY FIELD, written through the index",
     [], ["struct Sa { a: [i64; 2] }"],
     "    let mut w: Sa = Sa{a:[1i64, 2i64]};\n    let q: &mut [i64; 2] = &mut w.a;\n"
     "    q[0] = 77i64;\n    n = w.a[0]; r = w.a[1];\n")


json.dump(cells, open(os.path.join(OUT, "..", "cells.json"), "w"), indent=1)
print("cells: %d" % len(cells))
