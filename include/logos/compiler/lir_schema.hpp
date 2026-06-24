#pragma once

// Hermes schema for L-IR (LExpr / LStmt / Pattern).
//
// Phase 3a of the compiler-on-Hermes migration (see plans/snappy-knitting-kay.md).
// Registers schema_type_code variants and sparse keys for the on-zone TinyObjectMap
// representation of L-IR nodes. Currently codes-only — no mirror writers, no
// readers. Phase 3b/3c/3d will populate and consume the mirrors.
//
// Variant codes intentionally match the declaration order of the std::variant
// alternatives in lir.hpp; static_asserts in lir.cpp keep them in sync.

#include <cstdint>
#include <logos/core/named_code.hpp>

namespace logos::compiler::lir_schema {

using Key = logos::NamedCode<uint8_t>;

// ── LExpr variant codes ───────────────────────────────────────────────────
//
// Stored in the TinyObjectMap header via schema::lir_expr(int32_t(code)).
// Order MUST match LExpr::kind variant declaration in lir.hpp.

namespace expr {
enum class Code : int32_t {
    LitInt        = 0,
    LitFloat      = 1,
    LitBool       = 2,
    LitStr        = 3,
    VarRef        = 4,
    EnumLit       = 5,
    EnumLitData   = 6,
    Call          = 7,
    MethodCall    = 8,
    BinOp         = 9,
    Unary         = 10,
    AddrOf        = 11,
    AddrOfTemp    = 12,
    Deref         = 13,
    FieldRead     = 14,
    IndexRead     = 15,
    StructLit     = 16,
    ArrLit        = 17,
    Cast          = 18,
    // 19 retired: C++-style `new` heap-alloc expr (removed; no Rust equivalent)
    IfExpr        = 20,
    TupleLit      = 21,
    TupleIndex    = 22,
    SliceLit      = 23,
    SliceIndex    = 24,
    SliceLen      = 25,
    SlicePtr      = 26,
    ClosureBox    = 27,
    ClosureCall   = 28,
    FnPtrCall     = 29,
    FormatCall    = 30,
    PackExpand    = 31,
    Try           = 32,
    MatchExpr     = 33,
    SizeOf        = 34,
    TypeCodeOf    = 35,
    BlockExpr     = 36,
    HermesLit     = 37,
    PtrArith      = 38,
    PtrDiff       = 39,
    ReflectOf     = 40,
    AlignOf       = 41,
    GenericRef    = 42,
};
inline constexpr int32_t Count = 43;
} // namespace expr

// ── LStmt variant codes ───────────────────────────────────────────────────

namespace stmt {
enum class Code : int32_t {
    Let             = 0,
    Assign          = 1,
    Return          = 2,
    If              = 3,
    While           = 4,
    For             = 5,
    Loop            = 6,
    Break           = 7,
    Continue        = 8,
    Block           = 9,
    FieldWrite      = 10,
    IndexWrite      = 11,
    FieldIndexWrite = 12,
    ExprStmt        = 13,
    Match           = 14,
    // 15 retired: C++-style `delete` stmt (removed; no Rust equivalent)
    ForEach         = 16,
    DerefWrite      = 17,
    Drop            = 18,
    DerefFieldWrite = 19,
    TupleWrite      = 20,
    LetElse         = 21,
    ChainFieldWrite = 22,
};
inline constexpr int32_t Count = 23;
} // namespace stmt

// ── HermesVal variant codes ───────────────────────────────────────────────
//
// Synthetic category — HermesVal lives outside the LExpr/LStmt/Pattern enums
// but reuses the lir_expr() encoder with HV_BASE offset. See lir_mirror.cpp
// (LirMirrorEmitter::emit_hv) for the writer side.

namespace hermes_val {
inline constexpr int32_t HV_BASE = 200;
enum class Code : int32_t {
    Null    = HV_BASE + 0,
    Bool    = HV_BASE + 1,
    Int     = HV_BASE + 2,
    Float   = HV_BASE + 3,
    Str     = HV_BASE + 4,
    Map     = HV_BASE + 5,
    Array   = HV_BASE + 6,
    Capture = HV_BASE + 7,
    Type    = HV_BASE + 8,
};
} // namespace hermes_val

// ── Pattern variant codes ─────────────────────────────────────────────────

namespace pat {
enum class Code : int32_t {
    Variant     = 0,
    Int         = 1,
    Bool        = 2,
    Wild        = 3,
    VariantData = 4,
    Or          = 5,
    Tuple       = 6,
    Range       = 7,
    Struct      = 8,
    Slice       = 9,
    At          = 10,
    RefBind     = 11,
    RefPat      = 12,
};
inline constexpr int32_t Count = 13;
} // namespace pat

// ── Sparse keys ───────────────────────────────────────────────────────────
//
// One namespace per node category. Field keys are unique within a node; the
// numeric ids are stable and shared across variants of the same category to
// keep the TinyObjectMap key dictionary small.

// Keys common to every LExpr node.
namespace expr_common {
inline constexpr Key TYPE              {"TYPE",             0};   // RelPtr<LogosType> (TypeRef offset)
}

namespace expr_keys {
// Literals
inline constexpr Key LIT_I64           {"LIT_I64",          1};   // i64 (low half of the value)
inline constexpr Key LIT_I64_HI        {"LIT_I64_HI",       8};   // i64: HIGH half of a 128-bit LitInt
                                                                  // (i128/u128 literals). Absent ⇒ 0.
                                                                  // Reuses DISC slot — a LitInt never
                                                                  // carries an enum discriminant.
inline constexpr Key LIT_F64           {"LIT_F64",          2};   // f64
inline constexpr Key LIT_BOOL          {"LIT_BOOL",         3};   // u8
inline constexpr Key LIT_STR           {"LIT_STR",          4};   // Varchar

// Names / identifiers
inline constexpr Key NAME              {"NAME",             5};   // Varchar (var, field, method, callee, ...)
inline constexpr Key ENUM_NAME         {"ENUM_NAME",        6};   // Varchar
inline constexpr Key VARIANT           {"VARIANT",          7};   // Varchar
inline constexpr Key DISC              {"DISC",             8};   // i64 (discriminant)
inline constexpr Key STRUCT_NAME       {"STRUCT_NAME",      9};   // Varchar
// 10 reused: VAR_SLOT (was CLASS_NAME). Per-function dense variable slot index
// assigned by sema (shadowing/scope-aware), so borrow-check / mlir-gen can key
// var state on an integer instead of the variable NAME string. Absent (key not
// present) ⇒ -1 / unresolved (synthesized refs without a slot).
inline constexpr Key VAR_SLOT          {"VAR_SLOT",        10};   // Int

// Operators
inline constexpr Key OP                {"OP",              11};   // Varchar (binop, unary)
inline constexpr Key PTR_ARITH_OP      {"PTR_ARITH_OP",    12};   // u8 (EPtrArith::Op)

// Operands / sub-expressions (all RelPtr<LExpr>)
inline constexpr Key LHS               {"LHS",             13};
inline constexpr Key RHS               {"RHS",             14};
inline constexpr Key OPERAND           {"OPERAND",         15};
inline constexpr Key RECEIVER          {"RECEIVER",        16};
inline constexpr Key INDEX             {"INDEX",           17};
inline constexpr Key COND              {"COND",            18};
inline constexpr Key THEN_VAL          {"THEN_VAL",        19};
inline constexpr Key ELSE_VAL          {"ELSE_VAL",        20};
inline constexpr Key SCRUT             {"SCRUT",           21};
inline constexpr Key INNER             {"INNER",           22};
inline constexpr Key CALLEE            {"CALLEE",          23};
inline constexpr Key BASE_PTR          {"BASE_PTR",        24};
inline constexpr Key SLICE             {"SLICE",           25};
inline constexpr Key LEN               {"LEN",             26};
inline constexpr Key OFFSET            {"OFFSET",          27};
inline constexpr Key FMT               {"FMT",             28};
inline constexpr Key RESULT            {"RESULT",          29};

// Sequences (Hermes Array of RelPtr<LExpr>)
inline constexpr Key ARGS              {"ARGS",            30};
inline constexpr Key ELEMS             {"ELEMS",           31};
inline constexpr Key PAYLOAD           {"PAYLOAD",         32};
inline constexpr Key TYPE_ARGS         {"TYPE_ARGS",       33};   // Array<RelPtr<LogosType>>
inline constexpr Key ARG_TYPES         {"ARG_TYPES",       34};   // Array<RelPtr<LogosType>>
inline constexpr Key FIELD_NAMES       {"FIELD_NAMES",     35};   // Array<Varchar> (parallel to FIELD_VALUES)
inline constexpr Key FIELD_VALUES      {"FIELD_VALUES",    36};   // Array<RelPtr<LExpr>>
inline constexpr Key ARMS              {"ARMS",            37};   // Array<RelPtr<LMatchArm-mirror>>

// Method-call dispatch metadata
inline constexpr Key METHOD            {"METHOD",          38};   // Varchar
inline constexpr Key RESOLVED_SYMBOL   {"RESOLVED_SYMBOL", 39};   // Varchar
inline constexpr Key VTABLE_INDEX      {"VTABLE_INDEX",    40};   // i32
inline constexpr Key RESOLVED_TYPE     {"RESOLVED_TYPE",   41};   // Varchar
inline constexpr Key TAG_SYSTEM        {"TAG_SYSTEM",      42};   // Varchar
inline constexpr Key TAG_TRAIT         {"TAG_TRAIT",       43};   // Varchar

// Modifiers / flags
inline constexpr Key IS_MUT            {"IS_MUT",          44};   // u8 (EAddrOfTemp)
inline constexpr Key TUPLE_INDEX_VAL   {"TUPLE_INDEX_VAL", 45};   // u32

// ECast
inline constexpr Key HERMES_BUILD_FN   {"HERMES_BUILD_FN", 46};   // Varchar (empty for plain cast)

// EBlockExpr / EClosureBox
inline constexpr Key BLOCK             {"BLOCK",           47};   // RelPtr<LBlock-mirror>
inline constexpr Key CLOSURE           {"CLOSURE",         48};   // RelPtr<EClosure-mirror>

// ESizeOf / ETypeCodeOf / EReflectOf
inline constexpr Key ELEM_TYPE         {"ELEM_TYPE",       49};   // RelPtr<LogosType>

// ETry
inline constexpr Key OK_DISC           {"OK_DISC",         50};   // i32
inline constexpr Key ERR_DISC          {"ERR_DISC",        51};   // i32

} // namespace expr_keys

// ── Per-variant / per-mirror-map sub-namespaces ───────────────────────────
//
// TinyObjectMap caps unique key ids per map at MAX_KEYS=52 (52-bit bitmap in
// the 64-bit header). Variant-specific or mirror-specific keys live here so
// the global expr_keys numbering stays under 52. Each namespace's ids are
// only valid in maps of the corresponding category — they freely overlap with
// expr_keys numerically because they're never present in the same map.

// Keys for LMatchArm / EMatchArm mirror maps (separate map category).
namespace arm_keys {
inline constexpr Key PAT               {"ARM_PAT",          0};   // RelPtr<Pattern-mirror>
inline constexpr Key GUARD             {"ARM_GUARD",        1};   // RelPtr<LExpr> (optional)
inline constexpr Key VALUE             {"ARM_VALUE",        2};   // RelPtr<LExpr> (EMatchArm)
inline constexpr Key BODY              {"ARM_BODY",         3};   // RelPtr<LBlock> (LMatchArm)
} // namespace arm_keys

// Keys for the EHermesLit LExpr variant map. expr_common::TYPE at 0 still
// applies; these start at 1.
namespace hermes_lit_keys {
inline constexpr Key ROOT                {"ROOT",                1};   // RelPtr<HermesVal-mirror>
inline constexpr Key HAS_CAPTURES        {"HAS_CAPTURES",        2};   // u8
inline constexpr Key CAPTURE_EXPRS       {"CAPTURE_EXPRS",       3};   // Array<RelPtr<LExpr>>
inline constexpr Key CAPTURE_TYPES       {"CAPTURE_TYPES",       4};   // Array<RelPtr<LogosType>>
inline constexpr Key CAPTURE_PARAM_COUNT {"CAPTURE_PARAM_COUNT", 5};   // u32
inline constexpr Key STATIC_BLOB         {"STATIC_BLOB",         6};   // Varchar — pre-serialised Hermes blob (metacall HermesStatic splice). When non-empty: root/has_captures/etc are unused; codegen emits blob bytes directly into rodata with [u64 size][bytes] layout.
} // namespace hermes_lit_keys

// Keys for HermesVal mirror maps (HVNull / HVBool / HVInt / HVFloat / HVStr /
// HVMap / HVArray / HVCapture). HermesVal lives outside the LExpr variant
// space (synthetic HV_BASE category), so no expr_common::TYPE is reserved.
namespace hv_keys {
inline constexpr Key BOOL_VALUE        {"HV_BOOL",          0};   // u8
inline constexpr Key INT_VALUE         {"HV_I64",           1};   // i64
inline constexpr Key FLOAT_VALUE       {"HV_F64",           2};   // f64
inline constexpr Key STR_VALUE         {"HV_STR",           3};   // Varchar
inline constexpr Key MAP_KEYS          {"HV_MAP_KEYS",      4};   // Array<Varchar | i64>
inline constexpr Key MAP_VALUES        {"HV_MAP_VALUES",    5};   // Array<RelPtr<HermesVal-mirror>>
inline constexpr Key TYPE_NAME         {"HV_TYPE_NAME",     6};   // Varchar (HVMap key_type / HVArray elem_type)
inline constexpr Key ELEMS             {"HV_ELEMS",         7};   // Array<RelPtr<HermesVal-mirror>>
inline constexpr Key PARAM_INDEX       {"HV_PARAM_INDEX",   8};   // u32 (HVCapture)
inline constexpr Key VALUE_INDEX       {"HV_VALUE_INDEX",   9};   // u32 (HVCapture)
inline constexpr Key TYPE_KIND         {"HV_TYPE_KIND",    10};   // u32 (HVType.kind)
inline constexpr Key TYPE_UID          {"HV_TYPE_UID",     11};   // u64 (HVType.uid — first 8 bytes of full UID)
} // namespace hv_keys

// Keys for the EClosure synthetic mirror map.
namespace closure_keys {
inline constexpr Key BLOCK             {"CL_BLOCK",         0};   // RelPtr<LBlock>
inline constexpr Key NAME              {"CL_NAME",          1};   // Varchar (closure_id)
inline constexpr Key CAPTURE_TYPES     {"CL_CAPTURE_TYPES", 2};   // Array<RelPtr<LogosType>>
inline constexpr Key CAPTURE_NAMES     {"CL_CAPTURE_NAMES", 3};   // Array<Varchar>
inline constexpr Key RET_TYPE          {"CL_RET_TYPE",      4};   // RelPtr<LogosType>
inline constexpr Key IS_MOVE           {"CL_IS_MOVE",       5};   // u8
inline constexpr Key AS_FN_PTR         {"CL_AS_FN_PTR",     6};   // u8
inline constexpr Key PARAM_NAMES       {"CL_PARAM_NAMES",   7};   // Array<Varchar>
inline constexpr Key PARAM_TYPES       {"CL_PARAM_TYPES",   8};   // Array<RelPtr<LogosType>>
inline constexpr Key MUT_CAPTURES      {"CL_MUT_CAPTURES",  9};   // Array<u8> (per-capture: 1 if mutated in body)
inline constexpr Key ESCAPES           {"CL_ESCAPES",      10};   // u8 (G167-3b: boxed → heap env)
inline constexpr Key CAPTURE_PATHS     {"CL_CAPTURE_PATHS", 11}; // RFC-2229: per-capture dotted field path (`p.x`); parallel to PARAM_NAMES.
inline constexpr Key CAPTURE_FIELD_TYPES {"CL_CAPTURE_FIELD_TYPES", 12}; // RFC-2229 phase-2: per-capture FIELD type (null = whole-root); parallel.
} // namespace closure_keys

// Keys for the EPtrDiff LExpr variant map (in addition to expr_common::TYPE,
// expr_keys::LHS, expr_keys::RHS).
namespace ptrdiff_keys {
inline constexpr Key BY_BYTE           {"BY_BYTE",          1};   // u8
} // namespace ptrdiff_keys

// ── LStmt sparse keys ─────────────────────────────────────────────────────

namespace stmt_common {
inline constexpr Key LINE              {"LINE",             0};   // u32 (source line)
}

namespace stmt_keys {
// Names / labels
inline constexpr Key NAME              {"NAME",             1};   // Varchar (let/assign var, etc.)
inline constexpr Key LABEL             {"LABEL",            2};   // Varchar (loop label)
inline constexpr Key VAR               {"VAR",              3};   // Varchar (SFor/SForEach loop var)
inline constexpr Key RECEIVER          {"RECEIVER",         4};   // Varchar
inline constexpr Key FIELD             {"FIELD",            5};   // Varchar
inline constexpr Key MID_FIELD         {"MID_FIELD",        6};   // Varchar
inline constexpr Key TYPE_NAME         {"TYPE_NAME",        7};   // Varchar (SDerefFieldWrite class)
inline constexpr Key DROP_FN           {"DROP_FN",          8};   // Varchar
inline constexpr Key BREAK_SLOT        {"BREAK_SLOT",       9};   // Varchar

// Sub-nodes (RelPtr<LExpr> / RelPtr<LBlock>)
inline constexpr Key VALUE             {"VALUE",           10};
inline constexpr Key COND              {"COND",            11};
inline constexpr Key THEN_BLOCK        {"THEN_BLOCK",      12};
inline constexpr Key ELSE_BLOCK        {"ELSE_BLOCK",      13};
inline constexpr Key BODY              {"BODY",            14};
inline constexpr Key LO                {"LO",              15};
inline constexpr Key HI                {"HI",              16};
inline constexpr Key INDEX             {"INDEX",           17};
inline constexpr Key PTR               {"PTR",             18};
inline constexpr Key SCRUT             {"SCRUT",           19};
inline constexpr Key ELSE_DIVERGE      {"ELSE_DIVERGE",    20};   // RelPtr<LBlock> (SLetElse)
inline constexpr Key ITER              {"ITER",            21};
inline constexpr Key EXPR              {"EXPR",            22};   // SExprStmt

// Sub-pattern (SLetElse)
inline constexpr Key PAT               {"PAT",             23};   // RelPtr<Pattern>

// Match arms
inline constexpr Key ARMS              {"ARMS",            24};   // Array<RelPtr<LMatchArm>>

// Tuple write
inline constexpr Key TUPLE_INDEX_VAL   {"TUPLE_INDEX_VAL", 25};   // u32

// Type fields
inline constexpr Key TYPE              {"TYPE",            26};   // RelPtr<LogosType>
inline constexpr Key RECV_TYPE         {"RECV_TYPE",       27};   // RelPtr<LogosType>
inline constexpr Key ELEM_TYPE         {"ELEM_TYPE",       28};   // RelPtr<LogosType>
inline constexpr Key RESULT_TYPE       {"RESULT_TYPE",     29};   // RelPtr<LogosType> (SLoop)

// Flags
inline constexpr Key IS_MUT            {"IS_MUT",          30};   // u8
inline constexpr Key INCLUSIVE         {"INCLUSIVE",       31};   // u8
inline constexpr Key IS_SLICE          {"IS_SLICE",        32};   // u8
inline constexpr Key DROP_FIELDS       {"DROP_FIELDS",     33};   // u8
inline constexpr Key ARR_SIZE          {"ARR_SIZE",        34};   // i64
inline constexpr Key EXTRA_MIDS        {"EXTRA_MIDS",      35};   // Array<Varchar> — middle segments (between MID_FIELD and FIELD) in N-deep ChainFieldWrite
inline constexpr Key MOVED_FIELDS      {"MOVED_FIELDS",    36};   // Array<Varchar> — SDrop: field names of `var_name` that were moved out and must not be auto-dropped
inline constexpr Key DROP_OLD          {"DROP_OLD",        39};   // u8 — SAssign: drop the LHS's old value before storing (B8 drop-before-replace)
inline constexpr Key VAR_SLOT          {"VAR_SLOT",        40};   // Int — SLet/SFor binding's dense var slot (see expr_keys::VAR_SLOT). Absent ⇒ no slot.

// Multi-arena IR: dedicated per-element export ID. Stamped onto the
// TinyObjectMap of every externally-referenceable element (the published
// fn / method / template body block) at emit time, from a linear compile-time
// counter (== the element's directory obj_id). u32, stored as a U24 AnyVal
// (16M items/arena). Absent = not exported. This is the stable per-element
// handle a cross-arena ExternalRef will carry instead of a name lookup.
inline constexpr Key EXPORT_ID         {"EXPORT_ID",       37};   // u32 (U24 AnyVal)
// G161-3: refutable-inner guard exprs on a SLetElse (`let Some(1) = … else`).
// Each must hold (after the pattern's bindings are bound) or the else block
// runs; an empty/absent array means the disc/literal test is the whole check.
inline constexpr Key LET_ELSE_GUARDS   {"LET_ELSE_GUARDS", 38};   // Array<RelPtr<LExpr>>
} // namespace stmt_keys

// ── Declaration variant codes (Stage E: LProgram decl layer → Hermes mirror) ─
//
// Coarse top-level declarations (LFunction/LStructDef/LEnumDef/LConst/LTraitDef/
// LImplBlock/LTypeAlias). Encoded via the lir_stmt category with a DECL_BASE
// offset (out-of-band of real stmt codes 0..22 and the block Count=23), the same
// trick hermes_val uses (HV_BASE=200). Schema grows as each kind migrates.
namespace decl {
inline constexpr int32_t DECL_BASE = 300;
enum class Code : int32_t {
    TypeAlias = DECL_BASE + 0,
    Const     = DECL_BASE + 1,
    Func      = DECL_BASE + 2,
    Struct    = DECL_BASE + 3,
    Enum      = DECL_BASE + 4,
    Trait     = DECL_BASE + 5,
    Impl      = DECL_BASE + 6,
};
} // namespace decl

// ── Declaration sparse keys (shared across decl kinds; grows per kind) ───────
namespace decl_keys {
inline constexpr Key NAME     {"NAME",     1};   // Varchar
inline constexpr Key TYPE_REF {"TYPE_REF", 2};   // RelPtr<LogosType>  (alias/const/param type, ret type)
inline constexpr Key DOC      {"DOC",      3};   // Varchar (outer doc-comment)
inline constexpr Key VALUE     {"VALUE",     4};  // RelPtr<LExpr>  (const/static initializer)
inline constexpr Key IS_STATIC {"IS_STATIC", 5};  // bool (sparse: present only when true)
inline constexpr Key IS_MUT    {"IS_MUT",    6};  // bool (sparse) — `static mut`
inline constexpr Key IS_EXTERN {"IS_EXTERN", 7};  // bool (sparse) — extern-block decl
inline constexpr Key SYM       {"SYM",       8};  // Varchar — link symbol
// ── Enum (Code::Enum) decl keys ──────────────────────────────────────────
inline constexpr Key PKG             {"PKG",             9};  // Varchar — declaring package
inline constexpr Key ZONED2          {"ZONED2",          10}; // bool (sparse) — niche enum, Ref arm self-relative at-rest
inline constexpr Key BORROW_CARRYING {"BORROW_CARRYING", 11}; // bool (sparse) — HAny escape-tracked value
inline constexpr Key VARIANTS        {"VARIANTS",        12}; // Array<RelPtr<variant sub-map>>
inline constexpr Key TYPE_PARAMS     {"TYPE_PARAMS",     13}; // Array<RelPtr<typeparam sub-map>>
inline constexpr Key BACKING_TYPE    {"BACKING_TYPE",    14}; // RelPtr<LogosType> — C-style enum disc type (null=i32)
// ── Function (Code::Func) decl keys ──────────────────────────────────────
// Func reuses NAME=1, DOC=3, PKG=9, IS_EXTERN=7, TYPE_PARAMS=13 (array of
// fn_tparam sub-maps — richer element schema than the enum tparam array).
inline constexpr Key METHOD_BASE        {"METHOD_BASE",        15}; // Varchar — unmangled source method name
inline constexpr Key LIFETIME_PARAMS    {"LIFETIME_PARAMS",    16}; // Array<Varchar>
inline constexpr Key LIFETIME_OUTLIVES  {"LIFETIME_OUTLIVES",  17}; // Array<Varchar> flat pairs (2i=long, 2i+1=short)
inline constexpr Key PARAMS             {"PARAMS",             18}; // Array<RelPtr<param sub-map>>
inline constexpr Key RET_TYPE           {"RET_TYPE",           19}; // RelPtr<LogosType>
inline constexpr Key BODY               {"BODY",               20}; // RelPtr<block mirror>
inline constexpr Key LOCAL_COUNT        {"LOCAL_COUNT",        21}; // i64 (sparse: omit when 0)
inline constexpr Key IS_VARARG          {"IS_VARARG",          22}; // bool (sparse)
inline constexpr Key IS_PUB             {"IS_PUB",             23}; // bool (sparse)
inline constexpr Key IS_METAPROG_STUB   {"IS_METAPROG_STUB",   24}; // bool (sparse)
inline constexpr Key IS_SPECIALIZATION  {"IS_SPECIALIZATION",  25}; // bool (sparse)
inline constexpr Key SPEC_PATTERNS      {"SPEC_PATTERNS",      26}; // Array<RelPtr<LogosType>>
inline constexpr Key FROM_BINARY_MODULE {"FROM_BINARY_MODULE", 27}; // bool (sparse)
inline constexpr Key FROM_LAZY_MODULE   {"FROM_LAZY_MODULE",   28}; // bool (sparse)
inline constexpr Key SOURCE_FILE        {"SOURCE_FILE",        29}; // Varchar
inline constexpr Key IMPL_TYPE_PARAMS   {"IMPL_TYPE_PARAMS",   30}; // Array<RelPtr<fn_tparam sub-map>>
inline constexpr Key IMPL_TARGET_PATTERN{"IMPL_TARGET_PATTERN",31}; // RelPtr<LogosType>
inline constexpr Key WHERE_TYPE_BOUNDS  {"WHERE_TYPE_BOUNDS",  32}; // Array<RelPtr<wherebound sub-map>>
inline constexpr Key IS_TEST            {"IS_TEST",            33}; // bool (sparse)
inline constexpr Key SHOULD_PANIC       {"SHOULD_PANIC",       34}; // bool (sparse)
inline constexpr Key IGNORED            {"IGNORED",            35}; // bool (sparse)
inline constexpr Key SHOULD_PANIC_MSG   {"SHOULD_PANIC_MSG",   36}; // Varchar
inline constexpr Key BODY_EXTERNAL_REF  {"BODY_EXTERNAL_REF",  37}; // ExternalRef Pod niche (sparse: omit when invalid)
} // namespace decl_keys

// Function PARAM sub-map keys (own small key space — distinct map schema).
namespace param_keys {
inline constexpr Key P_NAME          {"P_NAME",          1};  // Varchar
inline constexpr Key P_TYPE          {"P_TYPE",          2};  // RelPtr<LogosType>
inline constexpr Key P_IS_VARIADIC   {"P_IS_VARIADIC",   3};  // bool (sparse)
inline constexpr Key P_OWNING_BOX_DYN{"P_OWNING_BOX_DYN",4};  // bool (sparse)
inline constexpr Key P_SLOT          {"P_SLOT",          5};  // i64 (sparse: omit when 0xFFFFFFFF)
} // namespace param_keys

// Function TYPE_PARAM sub-map keys (own space; richer than enum's — carries
// bounds + const + default, all read by mono/sema bound-checking).
namespace fn_tparam_keys {
inline constexpr Key FTP_NAME              {"FTP_NAME",              1};  // Varchar
inline constexpr Key FTP_IS_VARIADIC       {"FTP_IS_VARIADIC",       2};  // bool (sparse)
inline constexpr Key FTP_IS_CONST          {"FTP_IS_CONST",          3};  // bool (sparse)
inline constexpr Key FTP_CONST_TYPE        {"FTP_CONST_TYPE",        4};  // RelPtr<LogosType>
inline constexpr Key FTP_DEFAULT_TYPE      {"FTP_DEFAULT_TYPE",      5};  // RelPtr<LogosType>
inline constexpr Key FTP_BOUNDS            {"FTP_BOUNDS",            6};  // Array<RelPtr<tbound sub-map>>
inline constexpr Key FTP_LIFETIME_OUTLIVES {"FTP_LIFETIME_OUTLIVES", 7};  // Array<Varchar>
} // namespace fn_tparam_keys

// TraitBound sub-map keys (own space) — element schema of FTP_BOUNDS.
namespace fn_tbound_keys {
inline constexpr Key TB_TRAIT_NAME   {"TB_TRAIT_NAME",   1};  // Varchar
inline constexpr Key TB_TYPE_ARGS    {"TB_TYPE_ARGS",    2};  // Array<RelPtr<LogosType>>
inline constexpr Key TB_HRTB_BINDERS {"TB_HRTB_BINDERS", 3};  // Array<Varchar> — for<'a> binders
} // namespace fn_tbound_keys

// where_type_bounds sub-map keys (own space) — pair (subject type, trait name).
namespace fn_wherebound_keys {
inline constexpr Key WB_TYPE  {"WB_TYPE",  1};  // RelPtr<LogosType>
inline constexpr Key WB_TRAIT {"WB_TRAIT", 2};  // Varchar
} // namespace fn_wherebound_keys

// Enum VARIANT sub-map keys (own small key space — distinct map schema).
namespace variant_keys {
inline constexpr Key V_NAME         {"V_NAME",         1};  // Varchar
inline constexpr Key V_DISC         {"V_DISC",         2};  // i64
inline constexpr Key V_PAYLOAD_TYPES{"V_PAYLOAD_TYPES",3};  // Array<RelPtr<LogosType>>
inline constexpr Key V_IS_VARIADIC  {"V_IS_VARIADIC",  4};  // bool (sparse)
} // namespace variant_keys

// Enum TYPE_PARAM sub-map keys (own small key space). Only the fields READ
// post-construction off an enum template are stored (name + is_variadic);
// bounds/is_const/const_type/default_type are NOT read for enums (verified).
namespace enum_tparam_keys {
inline constexpr Key TP_NAME        {"TP_NAME",        1};  // Varchar
inline constexpr Key TP_IS_VARIADIC {"TP_IS_VARIADIC", 2};  // bool (sparse)
} // namespace enum_tparam_keys

// ── LStructDef (Code::Struct) decl keys — OWN key space ─────────────────────
// Struct is a distinct map schema; decl_keys already reaches 37 and a
// TinyObjectMap presence bitmap only holds key codes 0..51, so struct gets its
// own namespace restarting at 1 (codes never collide across distinct schemas).
namespace struct_keys {
inline constexpr Key NAME              {"NAME",              1};  // Varchar
inline constexpr Key PKG               {"PKG",               2};  // Varchar — declaring package
inline constexpr Key DOC               {"DOC",               3};  // Varchar — outer doc-comment
inline constexpr Key TYPE_PARAMS       {"TYPE_PARAMS",       4};  // Array<RelPtr<fn_tparam sub-map>> (REUSE fn_tparam schema)
inline constexpr Key LIFETIME_PARAMS   {"LIFETIME_PARAMS",   5};  // Array<Varchar>
inline constexpr Key LIFETIME_OUTLIVES {"LIFETIME_OUTLIVES", 6};  // Array<Varchar> flat pairs (2i=long, 2i+1=short)
inline constexpr Key FIELDS            {"FIELDS",            7};  // Array<RelPtr<field sub-map>>
inline constexpr Key METHODS           {"METHODS",           8};  // Array<RelPtr<func decl map>> (each = a FunctionView.addr())
inline constexpr Key TYPE_CODE         {"TYPE_CODE",         9};  // i64 (sparse: omit when 0)
inline constexpr Key TYPE_HASH         {"TYPE_HASH",         10}; // Varchar (23 raw bytes; omit when all-zero)
inline constexpr Key IS_PUB            {"IS_PUB",            11}; // bool (sparse)
inline constexpr Key IS_ZONED          {"IS_ZONED",          12}; // bool (sparse)
inline constexpr Key IS_DATA_PLAIN     {"IS_DATA_PLAIN",     13}; // bool (ALWAYS emitted — defaults true)
inline constexpr Key FROM_BINARY_MODULE{"FROM_BINARY_MODULE",14}; // bool (sparse)
inline constexpr Key IS_DST            {"IS_DST",            15}; // bool (sparse)
inline constexpr Key SELF_DESCRIBING   {"SELF_DESCRIBING",   16}; // bool (sparse)
inline constexpr Key REL_PTR           {"REL_PTR",           17}; // bool (sparse)
inline constexpr Key BORROW_CARRYING   {"BORROW_CARRYING",   18}; // bool (sparse)
inline constexpr Key NON_NULL          {"NON_NULL",          19}; // bool (sparse)
inline constexpr Key ZONE_MUT          {"ZONE_MUT",          20}; // bool (sparse)
inline constexpr Key ZONED2            {"ZONED2",            21}; // bool (sparse)
inline constexpr Key IS_UNION          {"IS_UNION",          22}; // bool (sparse)
inline constexpr Key REPR_TRANSPARENT  {"REPR_TRANSPARENT",  23}; // bool (sparse)
inline constexpr Key IS_ANNOTATION_TYPE{"IS_ANNOTATION_TYPE",24}; // bool (sparse)
inline constexpr Key ANNOTATIONS       {"ANNOTATIONS",       25}; // Array<RelPtr<annot sub-map>>
inline constexpr Key IS_SPECIALIZATION {"IS_SPECIALIZATION", 26}; // bool (sparse)
inline constexpr Key SPEC_PATTERNS     {"SPEC_PATTERNS",     27}; // Array<RelPtr<LogosType>>
} // namespace struct_keys

// LStructDef FIELD sub-map keys (own space — element schema of FIELDS).
namespace field_keys {
inline constexpr Key F_NAME        {"F_NAME",        1};  // Varchar
inline constexpr Key F_TYPE        {"F_TYPE",        2};  // RelPtr<LogosType>
inline constexpr Key F_IS_VARIADIC {"F_IS_VARIADIC", 3};  // bool (sparse)
inline constexpr Key F_DOC         {"F_DOC",         4};  // Varchar (sparse)
} // namespace field_keys

// LAnnotationInstance sub-map keys (own space — element schema of ANNOTATIONS).
namespace annot_keys {
inline constexpr Key A_NAME {"A_NAME", 1};  // Varchar — annotation datatype name
inline constexpr Key A_PKG  {"A_PKG",  2};  // Varchar — declaring package
inline constexpr Key A_FQN  {"A_FQN",  3};  // Varchar — resolved fully-qualified name
inline constexpr Key A_KV   {"A_KV",   4};  // Array<RelPtr<annkv sub-map>>
} // namespace annot_keys

// LAnnotationInstance KV-pair sub-map keys (own space — element schema of A_KV).
namespace annkv_keys {
inline constexpr Key KV_NAME  {"KV_NAME",  1};  // Varchar — field name
inline constexpr Key KV_VALUE {"KV_VALUE", 2};  // RelPtr<annval sub-map>
} // namespace annkv_keys

// LAnnotationValue sub-map keys (own space; RECURSIVE via AV_ARR). Reader keys
// off AV_KIND to decide which value field(s) are populated.
namespace annval_keys {
inline constexpr Key AV_KIND         {"AV_KIND",         1};  // i64 — LAnnotationValue::Kind
inline constexpr Key AV_I            {"AV_I",            2};  // i64 (Int / Bool)
inline constexpr Key AV_F            {"AV_F",            3};  // f64 (Float)
inline constexpr Key AV_S            {"AV_S",            4};  // Varchar (Str)
inline constexpr Key AV_ENUM_NAME    {"AV_ENUM_NAME",    5};  // Varchar (Enum)
inline constexpr Key AV_ENUM_VARIANT {"AV_ENUM_VARIANT", 6};  // Varchar (Enum)
inline constexpr Key AV_ARR          {"AV_ARR",          7};  // Array<RelPtr<annval sub-map>> (Array — RECURSIVE)
} // namespace annval_keys

// ── Pattern sparse keys ───────────────────────────────────────────────────

namespace pat_keys {
// Identifiers
inline constexpr Key NAME              {"NAME",             0};   // Varchar (PatWild, PatRefBind, PatAt)
inline constexpr Key ENUM_NAME         {"ENUM_NAME",        1};   // Varchar
inline constexpr Key VARIANT           {"VARIANT",          2};   // Varchar
inline constexpr Key STRUCT_NAME       {"STRUCT_NAME",      3};   // Varchar
inline constexpr Key FIELD_NAME        {"FIELD_NAME",       4};   // Varchar (PatFieldBinding mirror)

// Scalars
inline constexpr Key DISC              {"DISC",             5};   // i64
inline constexpr Key INT_VALUE         {"INT_VALUE",        6};   // i64 (PatInt)
inline constexpr Key BOOL_VALUE        {"BOOL_VALUE",       7};   // u8 (PatBool)
inline constexpr Key LO                {"LO",               8};   // i64 (PatRange)
inline constexpr Key HI                {"HI",               9};   // i64 (PatRange)

// Bindings (PatVariantData / PatTuple)
inline constexpr Key BINDINGS          {"BINDINGS",        10};   // Array<Varchar>
inline constexpr Key BINDING_TYPES     {"BINDING_TYPES",   11};   // Array<RelPtr<LogosType>>

// Sub-patterns
inline constexpr Key SUBS              {"SUBS",            12};   // Array<RelPtr<Pattern>> (PatTuple, PatOr alts)
inline constexpr Key SUB               {"SUB",             13};   // Array<RelPtr<Pattern>> (PatAt/PatRefPat 0..1, PatFieldBinding 0..1)
inline constexpr Key FIELDS            {"FIELDS",          14};   // Array<RelPtr<PatFieldBinding-mirror>>
inline constexpr Key PREFIX            {"PREFIX",          15};   // Array<RelPtr<Pattern>> (PatSlice)
inline constexpr Key REST              {"REST",            16};   // Array<RelPtr<Pattern>> (0|1, PatSlice)
inline constexpr Key SUFFIX            {"SUFFIX",          17};   // Array<RelPtr<Pattern>>
inline constexpr Key INNER             {"INNER",           18};   // Array<RelPtr<Pattern>> (PatRefPat, exactly 1)

// Flags
inline constexpr Key HAS_REST          {"HAS_REST",        19};   // u8 (PatStruct)
inline constexpr Key IS_MUT            {"IS_MUT",          20};   // u8 (PatRefBind, PatRefPat)

// Type fields
inline constexpr Key BIND_TYPE         {"BIND_TYPE",       21};   // RelPtr<LogosType> (PatRefBind)
inline constexpr Key TYPE              {"TYPE",            22};   // RelPtr<LogosType> (PatAt)
inline constexpr Key BIND_SLOTS        {"BIND_SLOTS",      23};   // Array<u32> — Phase-1 dense slots, parallel to BINDINGS (0xFFFFFFFF = none, e.g. `_`)
inline constexpr Key BIND_SLOT         {"BIND_SLOT",       24};   // u32 — Phase-1 dense slot for single-name patterns (Wild/At/RefBind)
} // namespace pat_keys

} // namespace logos::compiler::lir_schema
