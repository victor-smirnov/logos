# logos_writ_gdb.py — gdb decoder/pretty-printer for the Hermes container
# format (self-relative arena values). The compiler's IR (LProgram/AST) and any
# Hermes-backed runtime data use this format.
#
# Load:
#   (gdb) source /path/to/logos/tools/gdb/logos_writ_gdb.py
# Commands:
#   (gdb) logos-hermes <expr>     # decode the AnyVal word at &<expr> (or an addr)
# Also auto-registers a pretty-printer on `logos::writ::AnyVal`.
#
# Format (see include/logos/writ/*.hpp):
#   RelativePtr<T> : i64 at the field; abs = &field + offset (0 = null).
#   AnyVal (8B word at A):
#       word == 0                      -> null
#       word & 1 == 1                  -> Pod: code=(word>>1)&0x7F, val=word>>8 (i56)
#       word & 1 == 0 and word != 0    -> Ref: target = A + word (self-relative)
#   TypeTag (before an object at O): byte O[-1]=b;
#       b==0 unset; b<=222 -> code=b; else n=b-223+1, code=LE(O[-2-i] for i<n)
#   Containers (24B header unless noted), keyed by the target's TypeTag code:
#       100 ARRAY  {u64 size, u64 cap, relptr data->AnyVal[]}
#        98 TINYMAP{u64 header(bitmap[0:51],cap[52:57],size[58:63]), u64 schema, relptr data->AnyVal[]}
#       101 MAP    {i64 count, i64 cap, relptr data->MapEntry[16]{AnyVal key, AnyVal val}}
#       130 STRING varint length + UTF-8
#       102 DECIMAL{u32 spec(scale[0:11],sign[31]), u64 coeff}
#       2101..2110 TypedArray{u64 size,u64 cap,relptr data->T[]}
#       26 I64 27 U64 30 F32 31 F64 (boxed scalars, Ref target holds the value)

import struct

# ── Type codes (include/logos/writ/type_codes.hpp) ─────────────────────────
HA_I56, HA_BOOL = 1, 2
HT_I8, HT_U8, HT_I16, HT_I24, HT_U16, HT_U24 = 20, 21, 22, 23, 24, 25
BX_I64, BX_U64, BX_F32, BX_F64 = 26, 27, 30, 31
TINYMAP, ARRAY, MAP, DECIMAL, STRING = 98, 100, 101, 102, 130
ARRAY_U8, ARRAY_F64 = 2101, 2110

_POD_INT = {HA_I56, HT_I8, HT_U8, HT_I16, HT_I24, HT_U16, HT_U24}


class HermesDecoder:
    """Decodes the Hermes format given a `read(addr, size) -> bytes` callback."""
    def __init__(self, read, max_depth=12, max_items=64):
        self.read = read
        self.max_depth = max_depth
        self.max_items = max_items

    def u(self, addr, size):
        return int.from_bytes(self.read(addr, size), "little")

    def i64(self, addr):
        v = self.u(addr, 8)
        return v - (1 << 64) if v >> 63 else v

    # ── self-relative pointers ──
    def relptr(self, field_addr):
        off = self.i64(field_addr)
        return 0 if off == 0 else field_addr + off

    # ── TypeTag immediately before an object ──
    def tag(self, obj_addr):
        b = self.u(obj_addr - 1, 1)
        if b == 0:
            return 0
        if b <= 222:
            return b
        n = b - 223 + 1
        code = 0
        for i in range(n):
            code |= self.u(obj_addr - 2 - i, 1) << (8 * i)
        return code

    # ── varint (include/logos/writ/varint.hpp) ──
    def varint(self, addr):
        "Return (value, nbytes)."
        b = self.u(addr, 1)
        if b < 249:
            return b, 1
        n = b - 248
        return self.u(addr + 1, n), 1 + n

    # Is `addr` a plausible Hermes object start (its TypeTag is a known code)?
    # Used to disambiguate a live HAny (absolute Ref) from an at-rest AnyVal
    # (self-relative Ref) at the root, and as a bad-pointer guard.
    def _is_object(self, addr):
        if addr is None or addr <= 0:
            return False
        try:
            code = self.tag(addr)
        except Exception:
            return False
        return (code in (TINYMAP, ARRAY, MAP, DECIMAL, STRING, 26, 27, 30, 31, 127, 4115)
                or ARRAY_U8 <= code <= ARRAY_F64
                or 3101 <= code <= 3104)

    # ── AnyVal ──
    # live: the word is a LIVE HAny (compute form) — its Ref arm holds an
    # ABSOLUTE pointer. At-rest words inside an arena (the default, used for every
    # nested hop) hold a SELF-RELATIVE delta from their own slot. F3 storage/
    # compute split (#[zoned2] niche enum); see include/logos/writ/any_val.hpp
    # + project_f3_zoned_niche_enum.
    def anyval(self, addr, depth=0, live=False):
        word = self.u(addr, 8)
        if word == 0:
            return "null"
        if word & 1:  # Pod — identical in both forms
            code = (word >> 1) & 0x7F
            val = word >> 8
            if val >> 55:  # sign-extend i56
                val -= (1 << 56)
            if code == HA_BOOL:
                return "true" if val else "false"
            return str(val)
        # Ref arm.
        sword = word - (1 << 64) if word >> 63 else word
        rel = addr + sword
        if live:
            # Live HAny: Ref is absolute. Fall back to self-relative if that
            # isn't a real object (e.g. the value was actually at-rest).
            if self._is_object(word):
                return self.obj(word, depth)
            if self._is_object(rel):
                return self.obj(rel, depth)
            return "<ref 0x%x?>" % word
        return self.obj(rel, depth)

    def obj(self, addr, depth=0):
        if depth > self.max_depth:
            return "…"
        code = self.tag(addr)
        if code == STRING:
            length, nb = self.varint(addr)
            try:
                return '"%s"' % bytes(self.read(addr + nb, length)).decode("utf-8", "replace")
            except Exception:
                return '"<str>"'
        if code == ARRAY or (ARRAY_U8 <= code <= ARRAY_F64):
            n = self.u(addr, 8)
            data = self.relptr(addr + 16)
            out = []
            if data:
                if code == ARRAY:
                    for i in range(min(n, self.max_items)):
                        out.append(self.anyval(data + i * 8, depth + 1))
                else:
                    out.append("<%d typed elems>" % n)
            if n > self.max_items:
                out.append("… %d total" % n)
            return "[%s]" % ", ".join(out)
        if code == TINYMAP:
            header = self.u(addr, 8)
            bitmap = header & ((1 << 52) - 1)
            data = self.relptr(addr + 16)
            out = []
            if data:
                slot = 0
                for key in range(52):
                    if bitmap & (1 << key):
                        out.append("%d: %s" % (key, self.anyval(data + slot * 8, depth + 1)))
                        slot += 1
            return "{%s}" % ", ".join(out)
        if code == MAP:
            count = self.i64(addr)
            data = self.relptr(addr + 16)
            out = []
            if data:
                shown = 0
                # MapEntry[cap]; scan up to cap, skip null keys.
                cap = self.i64(addr + 8)
                for i in range(min(cap, 256)):
                    e = data + i * 16
                    if self.u(e, 8) == 0:
                        continue
                    out.append("%s: %s" % (self.anyval(e, depth + 1),
                                           self.anyval(e + 8, depth + 1)))
                    shown += 1
                    if shown >= self.max_items:
                        break
            return "{%s}" % ", ".join(out)
        if code == DECIMAL:
            spec = self.u(addr, 4)
            coeff = self.u(addr + 4, 8)
            scale = spec & 0xFFF
            neg = (spec >> 31) & 1
            return "%s%d/10^%d" % ("-" if neg else "", coeff, scale)
        if code == BX_I64:
            return str(self.i64(addr))
        if code == BX_U64:
            return str(self.u(addr, 8))
        if code in (BX_F32, BX_F64):
            sz = 4 if code == BX_F32 else 8
            raw = bytes(self.read(addr, sz))
            return str(struct.unpack("<f" if sz == 4 else "<d", raw)[0])
        return "<obj tc=%d>" % code


# ── gdb glue ─────────────────────────────────────────────────────────────────
def _gdb_reader():
    import gdb
    inf = gdb.selected_inferior()
    return lambda addr, size: bytes(inf.read_memory(int(addr), int(size)))


def _selftest():
    """Build a byte image and decode it — runs without gdb (python logos_writ_gdb.py)."""
    # Memory model: a dict-backed flat buffer.
    buf = bytearray(256)

    def wr(addr, data):
        buf[addr:addr + len(data)] = data

    def read(addr, size):
        return bytes(buf[addr:addr + size])

    d = HermesDecoder(read)

    # ArenaString "hi" at offset 16: tag byte at 15 = STRING(130), varint len=2, "hi".
    buf[15] = 130
    wr(16, bytes([2]) + b"hi")
    assert d.obj(16) == '"hi"', d.obj(16)

    # Pod AnyVal i56 = 42 at offset 64: word = (42<<8)|((1&0x7f)<<1)|1
    word = (42 << 8) | (HA_I56 << 1) | 1
    wr(64, word.to_bytes(8, "little"))
    assert d.anyval(64) == "42", d.anyval(64)

    # Pod bool true
    word = (1 << 8) | (HA_BOOL << 1) | 1
    wr(72, word.to_bytes(8, "little"))
    assert d.anyval(72) == "true", d.anyval(72)

    # Ref AnyVal at 80 → the string object at 16 (self-relative: 16-80 = -64).
    wr(80, (16 - 80).to_bytes(8, "little", signed=True))
    assert d.anyval(80) == '"hi"', d.anyval(80)

    # LIVE HAny at 88 → the SAME string, but absolute (compute form: word = 16,
    # NOT a self-relative delta). At-rest decode would mis-follow 88+16=104.
    wr(88, (16).to_bytes(8, "little"))
    assert d.anyval(88, live=True) == '"hi"', d.anyval(88, live=True)
    # The same absolute word decoded as at-rest must NOT spuriously match.
    assert d.anyval(88) != '"hi"', d.anyval(88)

    # ObjectArray [7, 8]: header at 100 (tag 100 at byte 99), elems at 128.
    pod = lambda v: ((v << 8) | (HA_I56 << 1) | 1).to_bytes(8, "little")
    wr(128, pod(7)); wr(136, pod(8))
    buf[99] = ARRAY
    wr(100, (2).to_bytes(8, "little"))            # size
    wr(108, (2).to_bytes(8, "little"))            # cap
    wr(116, (128 - 116).to_bytes(8, "little", signed=True))  # data relptr
    assert d.obj(100) == "[7, 8]", d.obj(100)

    # TinyObjectMap {3: 9}: header bitmap bit3 set, data at 184.
    wr(184, pod(9))
    buf[159] = TINYMAP
    wr(160, (1 << 3).to_bytes(8, "little"))       # header: bitmap bit 3
    wr(168, (0).to_bytes(8, "little"))            # schema_type_code
    wr(176, (184 - 176).to_bytes(8, "little", signed=True))  # data relptr
    assert d.obj(160) == "{3: 9}", d.obj(160)

    print("logos_hermes selftest: OK")


try:
    import gdb

    class LogosHermesCmd(gdb.Command):
        "logos-hermes <expr> — decode the Hermes AnyVal at &<expr> (or an address)."
        def __init__(self):
            super().__init__("logos-hermes", gdb.COMMAND_DATA)

        def invoke(self, arg, from_tty):
            v = gdb.parse_and_eval(arg)
            try:
                addr = int(v.address) if v.address is not None else int(v)
            except Exception:
                addr = int(v)
            # The argument is a LIVE value (an HAny local, or an address holding
            # a live word): its Ref arm is absolute. (Nested hops into the arena
            # are at-rest/self-relative; the decoder handles that automatically,
            # and falls back to self-relative if the root was actually at-rest.)
            print(HermesDecoder(_gdb_reader()).anyval(addr, live=True))

    class AnyValPrinter:
        def __init__(self, val):
            self.val = val

        def to_string(self):
            try:
                a = self.val.address
                if a is None:
                    return "<AnyVal>"
                return HermesDecoder(_gdb_reader()).anyval(int(a))
            except Exception:
                return "<AnyVal?>"

    def _av_lookup(val):
        t = val.type.strip_typedefs()
        name = (t.tag or t.name or "")
        if name.endswith("AnyVal") or name.endswith("writ::AnyVal"):
            return AnyValPrinter(val)
        return None

    LogosHermesCmd()
    gdb.printing.register_pretty_printer(None, _av_lookup, replace=True)
    print("logos: Hermes decoder loaded (logos-hermes <expr>; AnyVal printer)")

except ImportError:
    # Not under gdb — run the self-test.
    _selftest()
