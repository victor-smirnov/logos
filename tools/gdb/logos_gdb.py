# logos_gdb.py — gdb pretty-printers for Logos standard types.
#
# Load:
#   (gdb) source /path/to/logos/tools/gdb/logos_gdb.py
# or add that line to ~/.gdbinit. Requires a binary built with `logosc -g`.
#
# Covers String, Vec<T>, slices (&[T] / str), and Box<T>. These read fields
# emitted in DWARF by Stage 2-4 (typed pointers + members). Enum and Hermes
# container printers are driven by separate metadata (see logos_hermes_gdb.py).

import re
import gdb


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
