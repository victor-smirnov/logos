# logos_lldb.py — LLDB data formatters for Logos standard types.
#
# Load:
#   (lldb) command script import /path/to/logos/tools/gdb/logos_lldb.py
# or just use the `logos-lldb` wrapper, which does this for you. Requires a
# binary built with `logosc -g`.
#
# Mirrors the gdb printers (logos_gdb.py): summary + synthetic-children
# providers for String, Vec<T>, slices (&[T] / str), Box<T>, and enums. Enum
# layout/variant-name metadata is read from the `__logos_debug_meta` section the
# compiler emits (MLIR 20 can't express DWARF variant parts). Also adds a
# `logos-hermes <expr>` command decoding the Hermes container format.

import json
import re
import struct
import lldb


# ── memory / value helpers ───────────────────────────────────────────────────
def _read_mem(proc, addr, size):
    err = lldb.SBError()
    raw = proc.ReadMemory(int(addr), int(size), err)
    return bytes(raw) if (err.Success() and raw is not None) else None


def _u(b):
    return int.from_bytes(b, "little") if b else 0


def _val_addr(valobj):
    a = valobj.GetLoadAddress()
    return None if a == lldb.LLDB_INVALID_ADDRESS else a


# ── enum metadata (the __logos_debug_meta section) ───────────────────────────
_META = {}  # exe filename → {type name: record}


def _meta_for(target):
    key = (target.GetExecutable().GetFilename() or "?")
    if key in _META:
        return _META[key]
    merged = {}
    for i in range(target.GetNumModules()):
        sec = target.GetModuleAtIndex(i).FindSection(".logos_debug_meta")
        if not sec or not sec.IsValid():
            continue
        data = sec.GetSectionData()
        err = lldb.SBError()
        raw = data.ReadRawData(err, 0, data.GetByteSize())
        if not err.Success() or raw is None:
            continue
        for chunk in bytes(raw).split(b"\x00"):
            chunk = chunk.strip()
            if not chunk:
                continue
            try:
                merged.update(json.loads(chunk.decode("utf-8")))
            except Exception:
                pass
    _META[key] = merged
    return merged


# ── scalar rendering by Logos type name (no standalone DIE to cast through) ───
_INT_RE = re.compile(r"^([iu])(\d+)$")


def _type_size_align(tn):
    m = _INT_RE.match(tn)
    if m:
        b = (int(m.group(2)) + 7) // 8
        a = 1
        while a < b and a < 16:
            a <<= 1
        return a, a
    if tn in ("usize", "isize", "f64"):
        return 8, 8
    if tn == "f32":
        return 4, 4
    if tn == "bool":
        return 1, 1
    if tn == "char":
        return 4, 4
    return 8, 8


def _render_typed(proc, addr, tn):
    try:
        m = _INT_RE.match(tn)
        if m:
            signed = m.group(1) == "i"
            bits = int(m.group(2))
            raw = _u(_read_mem(proc, addr, (bits + 7) // 8))
            if signed and (raw >> (bits - 1)) & 1:
                raw -= (1 << bits)
            return str(raw)
        if tn == "usize":
            return str(_u(_read_mem(proc, addr, 8)))
        if tn == "isize":
            raw = _u(_read_mem(proc, addr, 8))
            return str(raw - (1 << 64) if raw >> 63 else raw)
        if tn == "bool":
            return "true" if _u(_read_mem(proc, addr, 1)) else "false"
        if tn == "char":
            cp = _u(_read_mem(proc, addr, 4))
            try:
                return "'%s'" % chr(cp)
            except Exception:
                return str(cp)
        if tn in ("f32", "f64"):
            sz = 4 if tn == "f32" else 8
            return str(struct.unpack("<f" if sz == 4 else "<d", _read_mem(proc, addr, sz))[0])
        return "0x%x" % _u(_read_mem(proc, addr, 8))
    except Exception:
        return "?"


def _read_utf8(proc, addr, n):
    if addr == 0 or n <= 0:
        return ""
    raw = _read_mem(proc, addr, n)
    return bytes(raw).decode("utf-8", "replace") if raw else "<unreadable>"


# ── summaries ────────────────────────────────────────────────────────────────
def string_summary(valobj, internal_dict):
    try:
        v = valobj.GetNonSyntheticValue()
        data = v.GetChildMemberWithName("data").GetValueAsUnsigned()
        n = v.GetChildMemberWithName("nbytes").GetValueAsSigned()
        return '"%s"' % _read_utf8(valobj.GetProcess(), data, n)
    except Exception:
        return "<String?>"


def vec_summary(valobj, internal_dict):
    try:
        n = valobj.GetNonSyntheticValue().GetChildMemberWithName("len").GetValueAsSigned()
        return "Vec(len=%d)" % n
    except Exception:
        return "<Vec?>"


def slice_summary(valobj, internal_dict):
    try:
        v = valobj.GetNonSyntheticValue()
        ptr = v.GetChildMemberWithName("ptr")
        n = v.GetChildMemberWithName("len").GetValueAsSigned()
        elem = ptr.GetType().GetPointeeType()
        if elem.GetByteSize() == 1 and elem.GetTypeClass() != lldb.eTypeClassStruct:
            return '"%s"' % _read_utf8(valobj.GetProcess(), ptr.GetValueAsUnsigned(), n)
        return "[%d elements]" % n
    except Exception:
        return "<slice?>"


def box_summary(valobj, internal_dict):
    try:
        p = valobj.GetNonSyntheticValue().GetChildMemberWithName("ptr")
        if p.GetValueAsUnsigned() == 0:
            return "Box(null)"
        return "Box(%s)" % p.Dereference().GetValue()
    except Exception:
        return "<Box?>"


def enum_summary(valobj, internal_dict):
    try:
        rec = _meta_for(valobj.GetTarget()).get(valobj.GetType().GetName())
        if not rec:
            return None
        addr = _val_addr(valobj)
        if addr is None:
            return None
        proc = valobj.GetProcess()
        kind = rec.get("kind")
        if kind in ("tagged", "c"):
            disc = _u(_read_mem(proc, addr + rec.get("disc_off", 0), rec.get("disc_size", 4)))
            for v in rec["variants"]:
                if v["d"] == disc:
                    f = v.get("f", [])
                    if not f:
                        return v["n"]
                    off = rec.get("payload_off", 0)
                    parts = []
                    for fn in f:
                        sz, al = _type_size_align(fn)
                        off = (off + al - 1) // al * al
                        parts.append(_render_typed(proc, addr + off, fn))
                        off += sz
                    return "%s(%s)" % (v["n"], ", ".join(parts))
            return "<%s #%d>" % (valobj.GetType().GetName(), disc)
        if kind == "niche_nullptr":
            word = _u(_read_mem(proc, addr, 8))
            if word == 0:
                return rec["none"]
            f = rec.get("some_f", [])
            return "%s(%s)" % (rec["some"], _render_typed(proc, addr, f[0]) if f else "0x%x" % word)
        if kind == "niche_lowbit":
            word = _u(_read_mem(proc, addr, 8))
            if word & 1:
                v = word if rec.get("val_raw") else (word >> 1)
                if not rec.get("val_raw"):
                    bits = rec.get("val_bits", 63)
                    if rec.get("val_signed") and (v >> (bits - 1)) & 1:
                        v -= (1 << bits)
                return "%s(%d)" % (rec["val_n"], v)
            return "%s(0x%x)" % (rec["ptr_n"], word)
        return None
    except Exception:
        return None


# ── synthetic children (expandable Vec / slice in the variable view) ──────────
class _SeqChildren:
    "Common ptr+len element provider; subclass sets has_cap."
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj.GetNonSyntheticValue()
        self.ptr = 0
        self.n = 0
        self.elem = None
        self.stride = 8

    def update(self):
        try:
            p = self.valobj.GetChildMemberWithName("ptr")
            self.ptr = p.GetValueAsUnsigned()
            self.elem = p.GetType().GetPointeeType()
            self.stride = self.elem.GetByteSize() or 8
            self.n = self.valobj.GetChildMemberWithName("len").GetValueAsSigned()
            if self.n < 0:
                self.n = 0
        except Exception:
            self.n = 0
        return False

    def num_children(self):
        return self.n

    def has_children(self):
        return True

    def get_child_index(self, name):
        try:
            return int(name.strip("[]"))
        except Exception:
            return -1

    def get_child_at_index(self, index):
        if index < 0 or index >= self.n or self.ptr == 0 or self.elem is None:
            return None
        return self.valobj.CreateValueFromAddress(
            "[%d]" % index, self.ptr + index * self.stride, self.elem)


class VecChildren(_SeqChildren):
    pass


class SliceChildren(_SeqChildren):
    pass


# ── `logos-hermes <expr>` : decode the Hermes container format ────────────────
# (Compact mirror of logos_hermes_gdb.py's decoder.)
def _hermes_decode(proc, addr, depth=0, live=True):
    def u(a, s):
        return _u(_read_mem(proc, a, s))

    def tag(o):
        b = u(o - 1, 1)
        if b == 0:
            return 0
        if b <= 222:
            return b
        n = b - 223 + 1
        return sum(u(o - 2 - i, 1) << (8 * i) for i in range(n))

    def is_object(a):
        if a is None or a <= 0:
            return False
        c = tag(a)
        return (c in (98, 100, 101, 102, 130, 26, 27, 30, 31, 127, 4115)
                or 2101 <= c <= 2110 or 3101 <= c <= 3104)

    # live: word is a LIVE HAny (Ref = absolute). Nested arena words are at-rest
    # (self-relative). See project_f3_zoned_niche_enum (storage/compute split).
    def anyval(a, d, lv=False):
        w = u(a, 8)
        if w == 0:
            return "null"
        if w & 1:
            code = (w >> 1) & 0x7F
            val = w >> 8
            if val >> 55:
                val -= (1 << 56)
            return ("true" if val else "false") if code == 2 else str(val)
        sw = w - (1 << 64) if w >> 63 else w
        rel = a + sw
        if lv:
            if is_object(w):
                return obj(w, d)
            if is_object(rel):
                return obj(rel, d)
            return "<ref 0x%x?>" % w
        return obj(rel, d)

    def obj(a, d):
        if d > 12:
            return "…"
        code = tag(a)
        if code == 130:  # STRING
            b = u(a, 1)
            if b < 249:
                ln, nb = b, 1
            else:
                nb = 1 + (b - 248)
                ln = u(a + 1, b - 248)
            raw = _read_mem(proc, a + nb, ln)
            return '"%s"' % (bytes(raw).decode("utf-8", "replace") if raw else "")
        if code == 100 or (2101 <= code <= 2110):  # ARRAY / TypedArray
            n = u(a, 8)
            off = u(a + 16, 8)
            data = (a + 16 + (off - (1 << 64) if off >> 63 else off)) if off else 0
            if code != 100:
                return "[%d typed elems]" % n
            return "[%s]" % ", ".join(anyval(data + i * 8, d + 1) for i in range(min(n, 64))) if data else "[]"
        if code == 98:  # TINYMAP
            header = u(a, 8)
            bitmap = header & ((1 << 52) - 1)
            off = u(a + 16, 8)
            data = (a + 16 + (off - (1 << 64) if off >> 63 else off)) if off else 0
            out = []
            slot = 0
            if data:
                for key in range(52):
                    if bitmap & (1 << key):
                        out.append("%d: %s" % (key, anyval(data + slot * 8, d + 1)))
                        slot += 1
            return "{%s}" % ", ".join(out)
        if code == 101:  # MAP
            cap = u(a + 8, 8)
            off = u(a + 16, 8)
            data = (a + 16 + (off - (1 << 64) if off >> 63 else off)) if off else 0
            out = []
            if data:
                for i in range(min(cap, 256)):
                    e = data + i * 16
                    if u(e, 8) == 0:
                        continue
                    out.append("%s: %s" % (anyval(e, d + 1), anyval(e + 8, d + 1)))
                    if len(out) >= 64:
                        break
            return "{%s}" % ", ".join(out)
        if code == 26:
            v = u(a, 8)
            return str(v - (1 << 64) if v >> 63 else v)
        if code == 27:
            return str(u(a, 8))
        if code in (30, 31):
            sz = 4 if code == 30 else 8
            return str(struct.unpack("<f" if sz == 4 else "<d", _read_mem(proc, a, sz))[0])
        return "<obj tc=%d>" % code

    return anyval(addr, depth, live)


def cmd_hermes(debugger, command, result, internal_dict):
    """logos-hermes <expr> — decode the Hermes AnyVal at &<expr> (or an address)."""
    target = debugger.GetSelectedTarget()
    frame = target.GetProcess().GetSelectedThread().GetSelectedFrame()
    v = frame.EvaluateExpression(command.strip())
    addr = v.GetLoadAddress()
    if addr == lldb.LLDB_INVALID_ADDRESS:
        addr = v.GetValueAsUnsigned()
    result.AppendMessage(_hermes_decode(target.GetProcess(), addr))


# ── registration ─────────────────────────────────────────────────────────────
def _register_enums(debugger):
    target = debugger.GetSelectedTarget()
    if not target or not target.IsValid():
        return 0
    n = 0
    for name in _meta_for(target):
        debugger.HandleCommand(
            'type summary add "%s" -F %s.enum_summary -w logos' % (name, __name__))
        n += 1
    return n


def cmd_register(debugger, command, result, internal_dict):
    """logos-register — (re)read enum metadata from the current target and register summaries."""
    result.AppendMessage("logos: registered %d enum summaries" % _register_enums(debugger))


def __lldb_init_module(debugger, internal_dict):
    m = __name__
    c = "logos"
    cmd = debugger.HandleCommand
    cmd('type summary add -x "^String$" -F %s.string_summary -w %s' % (m, c))
    cmd('type summary add -x "^Vec<"   -F %s.vec_summary   -w %s' % (m, c))
    cmd('type synthetic add -x "^Vec<" -l %s.VecChildren   -w %s' % (m, c))
    cmd('type summary add -x "^Box<"   -F %s.box_summary   -w %s' % (m, c))
    cmd(r'type summary add -x "^(&?\[|str$|&str$)" -F %s.slice_summary -w %s' % (m, c))
    cmd(r'type synthetic add -x "^&?\[" -l %s.SliceChildren -w %s' % (m, c))
    _register_enums(debugger)  # enums by exact name from __logos_debug_meta
    cmd("type category enable %s" % c)
    cmd("command script add -f %s.cmd_hermes logos-hermes" % m)
    cmd("command script add -f %s.cmd_register logos-register" % m)
    print("logos: lldb formatters loaded (String, Vec, slice, Box, enums; logos-hermes)")
