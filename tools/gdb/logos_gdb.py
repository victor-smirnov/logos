# logos_gdb.py — gdb pretty-printers for Logos standard types.
#
# Load:
#   (gdb) source /path/to/logos/tools/gdb/logos_gdb.py
# or add that line to ~/.gdbinit. Requires a binary built with `logosc -g`.
#
# Covers String, Vec<T>, slices (&[T] / str), and Box<T>. These read fields
# emitted in DWARF by Stage 2-4 (typed pointers + members). Enum and Hermes
# container printers are driven by separate metadata (see logos_writ_gdb.py).

import re
import json
import gdb

# ── Enum metadata (the `__logos_debug_meta` global emitted by `logosc -g`) ────
# MLIR 20 can't express DWARF variant parts, so enum layout/variant-name info is
# carried out-of-band and decoded here. Loaded lazily, cached.
_META = None
_META_TRIED = False


def _elf_section(path, secname):
    """Return raw bytes of an ELF64 section by name, or None."""
    import struct
    try:
        with open(path, "rb") as f:
            data = f.read()
    except Exception:
        return None
    if data[:4] != b"\x7fELF" or len(data) < 0x40 or data[4] != 2:
        return None  # ELF64 only
    e_shoff = struct.unpack_from("<Q", data, 0x28)[0]
    e_shentsize = struct.unpack_from("<H", data, 0x3A)[0]
    e_shnum = struct.unpack_from("<H", data, 0x3C)[0]
    e_shstrndx = struct.unpack_from("<H", data, 0x3E)[0]

    def shdr(i):  # → (name_off, file_off, size)
        b = e_shoff + i * e_shentsize
        nm, _typ, _fl, _addr, off, sz = struct.unpack_from("<IIQQQQ", data, b)
        return nm, off, sz

    _, sstr_off, _ = shdr(e_shstrndx)

    def sec_name(nm):
        end = data.index(b"\x00", sstr_off + nm)
        return data[sstr_off + nm:end].decode("latin1")

    for i in range(e_shnum):
        nm, off, sz = shdr(i)
        if sec_name(nm) == secname:
            return data[off:off + sz]
    return None


def _load_meta():
    global _META, _META_TRIED
    if _META_TRIED:
        return _META
    _META_TRIED = True
    merged = {}
    paths = []
    try:
        for o in gdb.objfiles():
            if o.filename:
                paths.append(o.filename)
    except Exception:
        pass
    for p in paths:
        blob = _elf_section(p, ".logos_debug_meta")
        if not blob:
            continue
        # Section = NUL-terminated JSON object(s), one per linked object file.
        for chunk in blob.split(b"\x00"):
            chunk = chunk.strip()
            if not chunk:
                continue
            try:
                merged.update(json.loads(chunk.decode("utf-8")))
            except Exception:
                pass
    _META = merged or None
    return _META


def _read_uint(addr, size):
    buf = gdb.selected_inferior().read_memory(int(addr), int(size))
    return int.from_bytes(bytes(buf), "little")


_INT_RE = re.compile(r"^([iu])(\d+)$")


def _align_up(v, a):
    return (v + a - 1) // a * a if a else v


def _type_size_align(tn):
    "Best-effort (size, align) in bytes for a Logos type name (x86-64)."
    m = _INT_RE.match(tn)
    if m:
        b = (int(m.group(2)) + 7) // 8
        a = 1
        while a < b and a < 16:
            a <<= 1
        return a, a  # alloc size rounds to alignment
    if tn in ("usize", "isize", "f64"):
        return 8, 8
    if tn == "f32":
        return 4, 4
    if tn == "bool":
        return 1, 1
    if tn == "char":
        return 4, 4
    return 8, 8  # pointers / opaque — best effort


def _render_typed(addr, tn):
    "Render a value of Logos type `tn` read from memory at addr."
    try:
        m = _INT_RE.match(tn)
        if m:
            signed = m.group(1) == "i"
            bits = int(m.group(2))
            raw = _read_uint(addr, (bits + 7) // 8)
            if signed and (raw >> (bits - 1)) & 1:
                raw -= (1 << bits)
            return str(raw)
        if tn == "usize":
            return str(_read_uint(addr, 8))
        if tn == "isize":
            raw = _read_uint(addr, 8)
            return str(raw - (1 << 64) if raw >> 63 else raw)
        if tn == "bool":
            return "true" if _read_uint(addr, 1) else "false"
        if tn == "char":
            cp = _read_uint(addr, 4)
            try:
                return "'%s'" % chr(cp)
            except Exception:
                return str(cp)
        if tn in ("f32", "f64"):
            import struct
            sz = 4 if tn == "f32" else 8
            raw = bytes(gdb.selected_inferior().read_memory(addr, sz))
            return str(struct.unpack("<f" if tn == "f32" else "<d", raw)[0])
        # Struct / named type: gdb may know it (named DIE).
        t = gdb.lookup_type(tn)
        return str(gdb.Value(addr).cast(t.pointer()).dereference())
    except Exception:
        return "0x%x" % _read_uint(addr, 8)


def _addr_of(val):
    try:
        if val.address is not None:
            return int(val.address)
    except Exception:
        pass
    return None


def _cast_at(addr, type_name):
    try:
        t = gdb.lookup_type(type_name)
        return gdb.Value(addr).cast(t.pointer()).dereference()
    except Exception:
        return None


def _read_utf8(ptr, nbytes):
    """Read nbytes from a char-ish pointer as a Python str (lossy-safe)."""
    if ptr == 0 or nbytes <= 0:
        return ""
    try:
        return ptr.string(encoding="utf-8", length=int(nbytes))
    except Exception:
        # Fall back to raw byte read.
        try:
            buf = gdb.selected_inferior().read_memory(int(ptr), int(nbytes))
            return bytes(buf).decode("utf-8", "replace")
        except Exception:
            return "<unreadable>"


class StringPrinter:
    "logos String { data: *u8, nbytes, cap }"
    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            # display_hint "string" makes gdb add the surrounding quotes.
            return _read_utf8(self.val["data"], self.val["nbytes"])
        except Exception:
            return "<String?>"

    def display_hint(self):
        return "string"


class VecPrinter:
    "logos Vec<T> { ptr: *T, len, cap } — also reused for slices (ptr/len)."
    def __init__(self, val, has_cap=True):
        self.val = val
        self.has_cap = has_cap

    def _ptr_len(self):
        return self.val["ptr"], int(self.val["len"])

    def to_string(self):
        try:
            _, n = self._ptr_len()
            return "Vec(len=%d)" % n if self.has_cap else "[%d elements]" % n
        except Exception:
            return "<Vec?>"

    def children(self):
        try:
            ptr, n = self._ptr_len()
            for i in range(n):
                yield ("[%d]" % i, (ptr + i).dereference())
        except Exception:
            return

    def display_hint(self):
        return "array"


class SlicePrinter:
    "logos &[T] / str fat pointer { ptr: *T, len }."
    def __init__(self, val):
        self.val = val

    def _elem_is_byte(self):
        try:
            t = self.val["ptr"].type.target().strip_typedefs()
            return t.code == gdb.TYPE_CODE_INT and t.sizeof == 1
        except Exception:
            return False

    def to_string(self):
        try:
            ptr = self.val["ptr"]
            n = int(self.val["len"])
            if self._elem_is_byte():
                return _read_utf8(ptr, n)  # display_hint "string" adds quotes
            return "[%d elements]" % n
        except Exception:
            return "<slice?>"

    def children(self):
        if self._elem_is_byte():
            return
        try:
            ptr = self.val["ptr"]
            n = int(self.val["len"])
            for i in range(n):
                yield ("[%d]" % i, (ptr + i).dereference())
        except Exception:
            return

    def display_hint(self):
        return "array" if not self._elem_is_byte() else "string"


class BoxPrinter:
    "logos Box<T> { ptr: *T } — show the pointee."
    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            p = self.val["ptr"]
            if int(p) == 0:
                return "Box(null)"
            return "Box(%s)" % str(p.dereference())
        except Exception:
            return "<Box?>"


class EnumPrinter:
    "Logos enum — decode discriminant/niche via __logos_debug_meta, show Variant(payload)."
    def __init__(self, val, meta):
        self.val = val
        self.meta = meta

    def _payload(self, addr, base_off, fields):
        vals = []
        off = base_off
        for fname in fields:
            sz, al = _type_size_align(fname)
            off = _align_up(off, al)
            vals.append(_render_typed(addr + off, fname))
            off += sz
        return vals

    def to_string(self):
        try:
            addr = _addr_of(self.val)
            if addr is None:
                return "<enum?>"
            kind = self.meta.get("kind")
            if kind in ("tagged", "c"):
                disc = _read_uint(addr + self.meta.get("disc_off", 0),
                                  self.meta.get("disc_size", 4))
                for v in self.meta["variants"]:
                    if v["d"] == disc:
                        f = v.get("f", [])
                        if not f:
                            return v["n"]
                        off = self.meta.get("payload_off", 0)
                        return "%s(%s)" % (v["n"], ", ".join(self._payload(addr, off, f)))
                return "<%s #%d>" % (self.val.type.tag or "enum", disc)
            if kind == "niche_nullptr":
                word = _read_uint(addr, 8)
                if word == 0:
                    return self.meta["none"]
                f = self.meta.get("some_f", [])
                if f:
                    pv = _cast_at(addr, f[0])
                    if pv is not None:
                        return "%s(%s)" % (self.meta["some"], str(pv))
                return "%s(0x%x)" % (self.meta["some"], word)
            if kind == "niche_lowbit":
                word = _read_uint(addr, 8)
                if word & 1:
                    if self.meta.get("val_raw"):
                        v = word
                    else:
                        v = word >> 1
                        bits = self.meta.get("val_bits", 63)
                        if self.meta.get("val_signed") and (v >> (bits - 1)) & 1:
                            v -= (1 << bits)
                    return "%s(%d)" % (self.meta["val_n"], v)
                return "%s(0x%x)" % (self.meta["ptr_n"], word)
            return "<enum?>"
        except Exception:
            return "<enum?>"


_VEC_RE = re.compile(r"^Vec<")
_SLICE_RE = re.compile(r"^&?\[.*\]$|^str$|^&str$")
_BOX_RE = re.compile(r"^Box<")


def _lookup(val):
    t = val.type.strip_typedefs()
    if t.code == gdb.TYPE_CODE_PTR or t.code == gdb.TYPE_CODE_REF:
        return None
    name = t.tag or t.name
    if not name:
        return None
    meta = _load_meta()
    if meta and name in meta:
        return EnumPrinter(val, meta[name])
    if name == "String":
        return StringPrinter(val)
    if _VEC_RE.match(name):
        return VecPrinter(val)
    if _BOX_RE.match(name):
        return BoxPrinter(val)
    if _SLICE_RE.match(name):
        return SlicePrinter(val)
    return None


def register(objfile=None):
    gdb.printing.register_pretty_printer(objfile, _Printer(), replace=True)


class _Printer(gdb.printing.PrettyPrinter):
    def __init__(self):
        super().__init__("logos")

    def __call__(self, val):
        try:
            return _lookup(val)
        except Exception:
            return None


# Register against the whole session so it applies to every Logos objfile.
import gdb.printing  # noqa: E402
register(None)
print("logos: pretty-printers loaded (String, Vec, slice, Box)")

# Also load the Hermes container decoder from the same directory.
try:
    import os
    _here = os.path.dirname(os.path.abspath(__file__))
    gdb.execute("source %s/logos_writ_gdb.py" % _here)
except Exception:
    pass
