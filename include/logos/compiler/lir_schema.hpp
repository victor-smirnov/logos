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
    New           = 19,
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
};
inline constexpr int32_t Count = 41;
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
    Delete          = 15,
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
inline constexpr Key LIT_I64           {"LIT_I64",          1};   // i64
inline constexpr Key LIT_F64           {"LIT_F64",          2};   // f64
inline constexpr Key LIT_BOOL          {"LIT_BOOL",         3};   // u8
inline constexpr Key LIT_STR           {"LIT_STR",          4};   // Varchar

// Names / identifiers
inline constexpr Key NAME              {"NAME",             5};   // Varchar (var, field, method, callee, ...)
inline constexpr Key ENUM_NAME         {"ENUM_NAME",        6};   // Varchar
inline constexpr Key VARIANT           {"VARIANT",          7};   // Varchar
inline constexpr Key DISC              {"DISC",             8};   // i64 (discriminant)
inline constexpr Key STRUCT_NAME       {"STRUCT_NAME",      9};   // Varchar
inline constexpr Key CLASS_NAME        {"CLASS_NAME",      10};   // Varchar (ENew)

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
inline constexpr Key EXPR              {"EXPR",            22};   // SExprStmt / SDelete

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
} // namespace stmt_keys

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
} // namespace pat_keys

} // namespace logos::compiler::lir_schema
