// Logos project — https://github.com/victor-smirnov/logos

#include "codegen.hpp"
#include "hrpc_idl_parser.hpp"

#include <logos/writ/view.hpp>
#include <logos/writ/compat.hpp>

#include <algorithm>
#include <format>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace logos::hrpc_gen {

using namespace logos::writ;
using namespace logos::hrpc::hrpc_idl_ast;

// ---------------------------------------------------------------------------
// Construction & collection
// ---------------------------------------------------------------------------

CodeGen::CodeGen(Hermes ast, std::string_view src_path)
    : ast_(std::move(ast)), src_path_(src_path)
{
    collect();
}

void CodeGen::collect() {
    auto root = ast_.root_object().as_tiny_map();

    // PACKAGE field → PACKAGE_DEF node → NAME → qualified_name
    AnyVal pkg_av = root.get(uint8_t(PACKAGE.code));
    if (!pkg_av.is_null()) {
        auto pkg_node = map_of(pkg_av);
        AnyVal name_av = pkg_node.get(uint8_t(NAME.code));
        if (!name_av.is_null())
            package_ = qualified_name(map_of(name_av));
    }
    cpp_ns_ = pkg_to_ns(package_);

    // ITEMS array — collect top-level definitions; skip nulls and PACKAGE_DEF/IMPORT_DEF.
    AnyVal items_av = root.get(uint8_t(ITEMS.code));
    if (items_av.is_null()) return;

    auto items = arr_of(items_av);
    for (uint64_t i = 0; i < items.size(); ++i) {
        AnyVal av = items.get(i);
        if (av.is_null()) continue;
        auto node = map_of(av);
        int32_t c = code_of(node);
        if (c == PACKAGE_DEF.code || c == IMPORT_DEF.code || c == OPTION_DEF.code) continue;
        items_.push_back(node);

        // Register name for type resolution.
        std::string name = str_field(node, NAME.code);
        if (c == MESSAGE.code)  message_names_.insert(name);
        if (c == ENUM_DEF.code) enum_names_.insert(name);
    }
}

// ---------------------------------------------------------------------------
// AST helpers
// ---------------------------------------------------------------------------

std::string CodeGen::str(AnyVal av) const {
    if (av.is_null()) return {};
    return std::string(StringView(av, ast_.holder()).view());
}

TinyMapView CodeGen::map_of(AnyVal av) const {
    return TinyMapView(av, ast_.holder());
}

ArrayView CodeGen::arr_of(AnyVal av) const {
    return ArrayView(av, ast_.holder());
}

int32_t CodeGen::code_of(TinyMapView node) const {
    AnyVal av = node.get(uint8_t(CODE.code));
    if (av.is_null()) return -1;
    return av.as_value<int32_t>();
}

std::string CodeGen::str_field(TinyMapView node, uint8_t key) const {
    if (!node.has_key(key)) return {};
    return str(node.get(key));
}

std::string CodeGen::qualified_name(TinyMapView type_ref_node) const {
    // TYPE_REF: ITEMS = [ident_node, ...]  where ident_node.NAME = string
    AnyVal items_av = type_ref_node.get(uint8_t(ITEMS.code));
    if (items_av.is_null()) return {};
    auto items = arr_of(items_av);
    std::string result;
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto part = map_of(items.get(i));
        if (i > 0) result += '.';
        result += str_field(part, NAME.code);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Type resolution
// ---------------------------------------------------------------------------

CodeGen::TypeInfo CodeGen::resolve_type(TinyMapView type_node) const {
    TypeInfo ti;
    int32_t c = code_of(type_node);

    if (c == STREAM_TYPE.code) {
        ti.is_stream = true;
        AnyVal inner_av = type_node.get(uint8_t(TYPE.code));
        if (!inner_av.is_null())
            ti = resolve_type(map_of(inner_av));
        ti.is_stream = true;
        return ti;
    }

    if (c == MAP_TYPE.code) {
        ti.is_map = true;
        ti.name    = "map";
        ti.cpp_get = "logos::writ::MapView";
        ti.cpp_set = "logos::writ::MapView";
        return ti;
    }

    // TYPE_REF: get simple name from first ident_node.
    std::string name = qualified_name(type_node);
    ti.name = name;

    // Built-in scalar types.
    struct ScalarMap { std::string_view idl; std::string cpp; };
    static constexpr ScalarMap kScalars[] = {
        {"bool",    "bool"},
        {"uint8",   "uint8_t"},
        {"uint16",  "uint16_t"},
        {"uint32",  "uint32_t"},
        {"int8",    "int8_t"},
        {"int16",   "int16_t"},
        {"int32",   "int32_t"},
        {"float32", "float"},
    };
    for (auto& [idl, cpp] : kScalars) {
        if (name == idl) {
            ti.is_scalar  = true;
            ti.cpp_get    = cpp;
            ti.cpp_set    = cpp;
            ti.scalar_cpp = cpp;
            return ti;
        }
    }

    // Large scalars — not yet supported in value mode; use TODO.
    static constexpr std::string_view kLarge[] = {"uint64", "int64", "float64"};
    for (auto& n : kLarge) {
        if (name == n) {
            ti.is_large   = true;
            ti.cpp_get    = "/* TODO: " + std::string(n) + " */";
            ti.cpp_set    = "/* TODO: " + std::string(n) + " */";
            return ti;
        }
    }

    // String / bytes.
    if (name == "string" || name == "bytes") {
        ti.is_string  = true;
        ti.cpp_get    = "logos::writ::StringView";
        ti.cpp_set    = "std::string_view";
        return ti;
    }

    // User-defined message.
    if (message_names_.count(name)) {
        ti.is_message = true;
        ti.cpp_get    = name;
        ti.cpp_set    = "const " + name + "&";
        return ti;
    }

    // User-defined enum.
    if (enum_names_.count(name)) {
        ti.is_enum    = true;
        ti.cpp_get    = name;
        ti.cpp_set    = name;
        ti.scalar_cpp = "uint32_t";
        return ti;
    }

    // Unknown — treat as opaque message.
    ti.is_message = true;
    ti.cpp_get    = name;
    ti.cpp_set    = "const " + name + "&";
    return ti;
}

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------

std::string CodeGen::pkg_to_ns(std::string_view pkg) {
    std::string ns(pkg);
    for (char& c : ns) if (c == '.') c = ':';
    // Replace single colons that aren't part of ::
    std::string out;
    for (size_t i = 0; i < ns.size(); ++i) {
        if (ns[i] == ':' && (i + 1 >= ns.size() || ns[i+1] != ':')) {
            out += "::";
        } else {
            out += ns[i];
        }
    }
    return out;
}

std::string CodeGen::upper_first(std::string_view s) {
    if (s.empty()) return {};
    std::string r(s);
    r[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(r[0])));
    return r;
}

std::string CodeGen::endpoint_const(std::string_view service, std::string_view method) const {
    return "k_" + std::string(service) + "_" + std::string(method) + "_id";
}

std::string CodeGen::endpoint_fqn(std::string_view service, std::string_view method) const {
    std::string fqn;
    if (!package_.empty()) { fqn += package_; fqn += '.'; }
    fqn += service;
    fqn += '/';
    fqn += method;
    return fqn;
}

// ---------------------------------------------------------------------------
// Header emission
// ---------------------------------------------------------------------------

void CodeGen::emit_header(std::ostream& out, std::string_view guard) const {
    out << "// Generated by hrpc_gen — DO NOT EDIT.\n";
    out << "// Source: " << src_path_ << "\n\n";
    out << "#pragma once\n\n";
    out << "#include <logos/hrpc/schema.hpp>\n";
    out << "#include <logos/hrpc/session.hpp>\n";
    out << "#include <logos/hrpc/context.hpp>\n";
    out << "#include <logos/writ/view.hpp>\n";
    out << "#include <logos/writ/compat.hpp>\n";
    out << "#include <string_view>\n";
    out << "#include <cstdint>\n\n";

    if (!cpp_ns_.empty()) {
        out << "namespace " << cpp_ns_ << " {\n\n";
    }

    for (auto& node : items_) {
        int32_t c = code_of(node);
        if (c == ENUM_DEF.code)  emit_enum_decl(out, node);
        if (c == MESSAGE.code)   emit_message_decl(out, node);
        if (c == SERVICE.code)   emit_service_decl(out, node);
    }

    if (!cpp_ns_.empty()) {
        out << "} // namespace " << cpp_ns_ << "\n";
    }
}

// ── Enum ──────────────────────────────────────────────────────────────────

void CodeGen::emit_enum_decl(std::ostream& out, TinyMapView node) const {
    std::string name = str_field(node, NAME.code);
    out << "enum class " << name << " : uint32_t {\n";

    AnyVal items_av = node.get(uint8_t(ITEMS.code));
    if (!items_av.is_null()) {
        auto items = arr_of(items_av);
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto val = map_of(items.get(i));
            std::string vname = str_field(val, NAME.code);
            std::string vval  = str_field(val, VALUE.code);
            out << "    " << vname << " = " << vval << ",\n";
        }
    }
    out << "};\n\n";
}

// ── Message ───────────────────────────────────────────────────────────────

void CodeGen::emit_message_decl(std::ostream& out, TinyMapView node) const {
    std::string name = str_field(node, NAME.code);

    out << "struct " << name << " {\n";
    out << "    logos::writ::Hermes  doc;\n";
    out << "    logos::writ::TinyMapView map;\n\n";

    AnyVal items_av = node.get(uint8_t(ITEMS.code));

    out << "    static " << name << " make();\n";
    out << "    static " << name << " from_doc(logos::writ::Hermes doc);\n\n";

    // Getters / setters for each field.
    if (!items_av.is_null()) {
        auto items = arr_of(items_av);
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto field = map_of(items.get(i));
            if (code_of(field) != FIELD.code) continue;

            std::string label = str_field(field, LABEL.code);
            std::string fname = str_field(field, NAME.code);

            AnyVal type_av = field.get(uint8_t(TYPE.code));
            if (type_av.is_null()) continue;
            TinyMapView type_node = map_of(type_av);
            bool is_map_field  = (code_of(type_node) == MAP_TYPE.code);
            bool is_repeated   = (label == "repeated");
            bool is_optional   = (label == "optional");

            if (is_map_field) {
                // map<K, V> — string-keyed ObjectMap, V determines typed accessors.
                // Only map<string, T> is supported (string keys → ObjectMap).
                AnyVal val_av = type_node.get(uint8_t(VALUE_TYPE.code));
                if (val_av.is_null()) continue;
                TypeInfo vti = resolve_type(map_of(val_av));
                out << "    logos::writ::MapView " << fname << "_map() const;\n";
                if (!vti.is_large) {
                    out << "    " << vti.cpp_get << " " << fname
                        << "(std::string_view key) const;\n";
                    out << "    void set_" << fname << "(std::string_view key, "
                        << vti.cpp_set << " v);\n";
                }
            } else if (is_repeated) {
                TypeInfo ti = resolve_type(type_node);
                if (!ti.is_large) {
                    out << "    uint64_t " << fname << "_size() const;\n";
                    out << "    logos::writ::ArrayView " << fname << "_array() const;\n";
                    out << "    " << ti.cpp_get << " " << fname << "(uint64_t i) const;\n";
                    out << "    void add_" << fname << "(" << ti.cpp_set << " v);\n";
                } else {
                    out << "    // repeated " << fname << ": " << ti.name << " not yet supported\n";
                }
            } else {
                TypeInfo ti = resolve_type(type_node);
                if (is_optional) out << "    bool has_" << fname << "() const;\n";
                if (!ti.is_large) {
                    out << "    " << ti.cpp_get << " " << fname << "() const;\n";
                    out << "    void set_" << fname << "(" << ti.cpp_set << " v);\n";
                } else {
                    out << "    // " << fname << ": " << ti.name << " not yet supported\n";
                }
            }
        }
    }

    out << "};\n\n";
}

// ── Service ───────────────────────────────────────────────────────────────

void CodeGen::emit_service_decl(std::ostream& out, TinyMapView node) const {
    std::string svc  = str_field(node, NAME.code);
    std::string hname = svc + "Handler";
    std::string cname = svc + "Client";

    // Handler interface.
    out << "// Server-side handler — implement this and register with register_"
        << svc << "_service().\n";
    out << "class " << hname << " {\npublic:\n";
    out << "    virtual ~" << hname << "() = default;\n\n";

    AnyVal items_av = node.get(uint8_t(ITEMS.code));
    if (!items_av.is_null()) {
        auto items = arr_of(items_av);
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto rpc = map_of(items.get(i));
            if (code_of(rpc) != RPC_METHOD.code) continue;
            out << "    virtual logos::hrpc::Response "
                << rpc_handler_sig(rpc) << " = 0;\n";
        }
    }
    out << "};\n\n";

    // Registration function.
    out << "void register_" << svc << "_service(\n";
    out << "    logos::hrpc::Session& session, " << hname << "& handler);\n\n";

    // Client proxy.
    out << "class " << cname << " {\npublic:\n";
    out << "    explicit " << cname << "(logos::hrpc::Session& session);\n\n";
    if (!items_av.is_null()) {
        auto items = arr_of(items_av);
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto rpc = map_of(items.get(i));
            if (code_of(rpc) != RPC_METHOD.code) continue;
            out << "    logos::hrpc::Response " << rpc_client_sig(rpc) << ";\n";
        }
    }
    out << "\nprivate:\n";
    out << "    logos::hrpc::Session& session_;\n";
    out << "};\n\n";
}

// ---------------------------------------------------------------------------
// Source emission
// ---------------------------------------------------------------------------

void CodeGen::emit_source(std::ostream& out, std::string_view include_name) const {
    out << "// Generated by hrpc_gen — DO NOT EDIT.\n";
    out << "// Source: " << src_path_ << "\n\n";
    out << "#include \"" << include_name << "\"\n\n";

    if (!cpp_ns_.empty()) {
        out << "namespace " << cpp_ns_ << " {\n\n";
    }

    for (auto& node : items_) {
        int32_t c = code_of(node);
        if (c == MESSAGE.code) emit_message_impl(out, node);
        if (c == SERVICE.code) emit_service_impl(out, node);
    }

    if (!cpp_ns_.empty()) {
        out << "} // namespace " << cpp_ns_ << "\n";
    }
}

// ── Message impl ──────────────────────────────────────────────────────────

void CodeGen::emit_message_impl(std::ostream& out, TinyMapView node) const {
    std::string name = str_field(node, NAME.code);

    // Count fields.
    int field_count = 0;
    AnyVal items_av = node.get(uint8_t(ITEMS.code));
    if (!items_av.is_null()) {
        auto items = arr_of(items_av);
        for (uint64_t i = 0; i < items.size(); ++i)
            if (code_of(map_of(items.get(i))) == FIELD.code) ++field_count;
    }

    // make()
    out << name << " " << name << "::make() {\n";
    out << "    " << name << " m;\n";
    out << "    m.doc = logos::writ::make_doc();\n";
    out << "    auto root = m.doc.make_tiny_map(" << field_count << ");\n";
    out << "    m.doc.set_root(root);\n";
    out << "    m.map = root;\n";
    out << "    return m;\n";
    out << "}\n\n";

    // from_doc()
    out << name << " " << name << "::from_doc(logos::writ::Hermes doc) {\n";
    out << "    " << name << " m;\n";
    out << "    m.doc = std::move(doc);\n";
    out << "    m.map = m.doc.root_object().as_tiny_map();\n";
    out << "    return m;\n";
    out << "}\n\n";

    // Field getters / setters.
    if (!items_av.is_null()) {
        auto items = arr_of(items_av);
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto field = map_of(items.get(i));
            if (code_of(field) != FIELD.code) continue;

            std::string label = str_field(field, LABEL.code);
            std::string fname = str_field(field, NAME.code);
            std::string fnum  = str_field(field, NUMBER.code);

            AnyVal type_av = field.get(uint8_t(TYPE.code));
            if (type_av.is_null()) continue;
            TinyMapView type_node = map_of(type_av);
            uint8_t slot = static_cast<uint8_t>(std::stoi(fnum));

            bool is_map_field = (code_of(type_node) == MAP_TYPE.code);
            bool is_repeated  = (label == "repeated");
            bool is_opt       = (label == "optional");

            // ── map<string, V> ──────────────────────────────────────────
            if (is_map_field) {
                AnyVal val_av = type_node.get(uint8_t(VALUE_TYPE.code));
                if (val_av.is_null()) continue;
                TypeInfo vti = resolve_type(map_of(val_av));
                if (vti.is_large) continue;

                // {field}_map()
                out << "logos::writ::MapView " << name << "::" << fname << "_map() const {\n";
                out << "    return logos::writ::MapView("
                    << "map.get(uint8_t(" << int(slot) << ")).to_offset(), doc.holder());\n";
                out << "}\n\n";

                // {field}(key)
                out << vti.cpp_get << " " << name << "::" << fname << "(std::string_view key) const {\n";
                out << "    logos::writ::MapView m("
                    << "map.get(uint8_t(" << int(slot) << ")).to_offset(), doc.holder());\n";
                if (vti.is_scalar) {
                    out << "    return m.get(key).as_value<" << vti.scalar_cpp << ">();\n";
                } else if (vti.is_enum) {
                    out << "    return static_cast<" << vti.name
                        << ">(m.get(key).as_value<uint32_t>());\n";
                } else if (vti.is_string) {
                    out << "    return logos::writ::StringView(m.get(key).to_offset(), doc.holder());\n";
                } else if (vti.is_message) {
                    out << "    " << vti.name << " nested;\n";
                    out << "    nested.doc = doc;\n";
                    out << "    nested.map = logos::writ::TinyMapView(m.get(key).to_offset(), doc.holder());\n";
                    out << "    return nested;\n";
                }
                out << "}\n\n";

                // set_{field}(key, v)
                out << "void " << name << "::set_" << fname
                    << "(std::string_view key, " << vti.cpp_set << " v) {\n";
                out << "    if (!map.has_key(uint8_t(" << int(slot) << "))) {\n";
                out << "        map.put(uint8_t(" << int(slot)
                    << "), doc.make_object_map().to_anyval());\n";
                out << "    }\n";
                out << "    logos::writ::MapView m("
                    << "map.get(uint8_t(" << int(slot) << ")).to_offset(), doc.holder());\n";
                if (vti.is_scalar) {
                    out << "    m.put(key, logos::writ::AnyVal::from_value(v));\n";
                } else if (vti.is_enum) {
                    out << "    m.put(key, logos::writ::AnyVal::from_value(static_cast<uint32_t>(v)));\n";
                } else if (vti.is_string) {
                    out << "    m.put(key, doc.make_string(v).to_anyval());\n";
                } else if (vti.is_message) {
                    out << "    m.put(key, v.map.to_anyval());\n";
                }
                out << "}\n\n";
                continue;
            }

            TypeInfo ti = resolve_type(type_node);
            if (ti.is_large) continue;

            // ── repeated T ───────────────────────────────────────────────
            if (is_repeated) {
                // {field}_size()
                out << "uint64_t " << name << "::" << fname << "_size() const {\n";
                out << "    if (!map.has_key(uint8_t(" << int(slot) << "))) return 0;\n";
                out << "    return logos::writ::ArrayView("
                    << "map.get(uint8_t(" << int(slot) << ")).to_offset(), doc.holder()).size();\n";
                out << "}\n\n";

                // {field}_array()
                out << "logos::writ::ArrayView " << name << "::" << fname << "_array() const {\n";
                out << "    return logos::writ::ArrayView("
                    << "map.get(uint8_t(" << int(slot) << ")).to_offset(), doc.holder());\n";
                out << "}\n\n";

                // {field}(i) — typed element getter
                out << ti.cpp_get << " " << name << "::" << fname << "(uint64_t i) const {\n";
                out << "    logos::writ::ArrayView arr("
                    << "map.get(uint8_t(" << int(slot) << ")).to_offset(), doc.holder());\n";
                if (ti.is_scalar) {
                    out << "    return arr.get(i).as_value<" << ti.scalar_cpp << ">();\n";
                } else if (ti.is_enum) {
                    out << "    return static_cast<" << ti.name
                        << ">(arr.get(i).as_value<uint32_t>());\n";
                } else if (ti.is_string) {
                    out << "    return logos::writ::StringView(arr.get(i).to_offset(), doc.holder());\n";
                } else if (ti.is_message) {
                    out << "    " << ti.name << " nested;\n";
                    out << "    nested.doc = doc;\n";
                    out << "    nested.map = logos::writ::TinyMapView(arr.get(i).to_offset(), doc.holder());\n";
                    out << "    return nested;\n";
                }
                out << "}\n\n";

                // add_{field}(v) — lazy-creates the array on first call
                out << "void " << name << "::add_" << fname << "(" << ti.cpp_set << " v) {\n";
                out << "    if (!map.has_key(uint8_t(" << int(slot) << "))) {\n";
                out << "        map.put(uint8_t(" << int(slot)
                    << "), doc.make_array(4).to_anyval());\n";
                out << "    }\n";
                out << "    logos::writ::ArrayView arr("
                    << "map.get(uint8_t(" << int(slot) << ")).to_offset(), doc.holder());\n";
                if (ti.is_scalar) {
                    out << "    arr.push_back(logos::writ::AnyVal::from_value(v));\n";
                } else if (ti.is_enum) {
                    out << "    arr.push_back(logos::writ::AnyVal::from_value(static_cast<uint32_t>(v)));\n";
                } else if (ti.is_string) {
                    out << "    arr.push_back(doc.make_string(v).to_anyval());\n";
                } else if (ti.is_message) {
                    out << "    arr.push_back(v.map.to_anyval());\n";
                }
                out << "}\n\n";
                continue;
            }

            // ── singular field ────────────────────────────────────────────
            if (is_opt) {
                out << "bool " << name << "::has_" << fname << "() const {\n";
                out << "    return map.has_key(uint8_t(" << int(slot) << "));\n";
                out << "}\n\n";
            }

            // Getter
            out << ti.cpp_get << " " << name << "::" << fname << "() const {\n";
            if (ti.is_scalar) {
                out << "    return map.get(uint8_t(" << int(slot) << ")).as_value<"
                    << ti.scalar_cpp << ">();\n";
            } else if (ti.is_enum) {
                out << "    return static_cast<" << ti.name
                    << ">(map.get(uint8_t(" << int(slot) << ")).as_value<uint32_t>());\n";
            } else if (ti.is_string) {
                out << "    auto av = map.get(uint8_t(" << int(slot) << "));\n";
                out << "    return logos::writ::StringView(av.to_offset(), doc.holder());\n";
            } else if (ti.is_message) {
                out << "    " << ti.name << " nested;\n";
                out << "    nested.doc = doc;\n";
                out << "    auto av = map.get(uint8_t(" << int(slot) << "));\n";
                out << "    nested.map = logos::writ::TinyMapView(av.to_offset(), doc.holder());\n";
                out << "    return nested;\n";
            }
            out << "}\n\n";

            // Setter
            out << "void " << name << "::set_" << fname << "(" << ti.cpp_set << " v) {\n";
            if (ti.is_scalar) {
                out << "    map.put(uint8_t(" << int(slot) << "), logos::writ::AnyVal::from_value(v));\n";
            } else if (ti.is_enum) {
                out << "    map.put(uint8_t(" << int(slot)
                    << "), logos::writ::AnyVal::from_value(static_cast<uint32_t>(v)));\n";
            } else if (ti.is_string) {
                out << "    map.put(uint8_t(" << int(slot) << "), doc.make_string(v).to_anyval());\n";
            } else if (ti.is_message) {
                out << "    map.put(uint8_t(" << int(slot) << "), v.map.to_anyval());\n";
            }
            out << "}\n\n";
        }
    }
}

// ── Service impl ──────────────────────────────────────────────────────────

void CodeGen::emit_service_impl(std::ostream& out, TinyMapView node) const {
    std::string svc   = str_field(node, NAME.code);
    std::string hname = svc + "Handler";
    std::string cname = svc + "Client";

    AnyVal items_av = node.get(uint8_t(ITEMS.code));

    // Endpoint ID constants.
    if (!items_av.is_null()) {
        auto items = arr_of(items_av);
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto rpc = map_of(items.get(i));
            if (code_of(rpc) != RPC_METHOD.code) continue;
            std::string method = str_field(rpc, NAME.code);
            std::string fqn    = endpoint_fqn(svc, method);
            std::string cst    = endpoint_const(svc, method);
            out << "static const logos::hrpc::EndpointID " << cst
                << " = logos::hrpc::endpoint_id_from_name(\"" << fqn << "\");\n";
        }
        out << "\n";
    }

    // register_*_service()
    out << "void register_" << svc << "_service(\n";
    out << "    logos::hrpc::Session& session, " << hname << "& handler)\n{\n";

    if (!items_av.is_null()) {
        auto items = arr_of(items_av);
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto rpc = map_of(items.get(i));
            if (code_of(rpc) != RPC_METHOD.code) continue;

            std::string method = str_field(rpc, NAME.code);
            std::string cst    = endpoint_const(svc, method);

            // Determine input type name (if unary).
            std::string input_type;
            AnyVal in_av = rpc.get(uint8_t(INPUT.code));
            if (!in_av.is_null()) {
                TypeInfo ti = resolve_type(map_of(in_av));
                if (!ti.is_stream && ti.is_message) input_type = ti.name;
            }

            out << "    session.endpoints().add(" << cst
                << ", [&handler](logos::hrpc::Context& ctx) -> logos::hrpc::Response {\n";

            if (!input_type.empty()) {
                out << "        auto req = " << input_type
                    << "::from_doc(ctx.request().doc);\n";
                out << "        return handler." << method << "(req, ctx);\n";
            } else {
                out << "        return handler." << method << "(ctx);\n";
            }
            out << "    });\n";
        }
    }
    out << "}\n\n";

    // Client constructor.
    out << cname << "::" << cname << "(logos::hrpc::Session& session)\n";
    out << "    : session_(session) {}\n\n";

    // Client methods.
    if (!items_av.is_null()) {
        auto items = arr_of(items_av);
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto rpc = map_of(items.get(i));
            if (code_of(rpc) != RPC_METHOD.code) continue;

            std::string method = str_field(rpc, NAME.code);
            std::string cst    = endpoint_const(svc, method);

            AnyVal in_av  = rpc.get(uint8_t(INPUT.code));
            AnyVal out_av = rpc.get(uint8_t(OUTPUT.code));

            TypeInfo in_ti, out_ti;
            if (!in_av.is_null())  in_ti  = resolve_type(map_of(in_av));
            if (!out_av.is_null()) out_ti = resolve_type(map_of(out_av));

            bool client_streams = in_ti.is_stream;
            bool server_streams = out_ti.is_stream;

            out << "logos::hrpc::Response " << cname << "::" << rpc_client_sig(rpc) << " {\n";

            if (!client_streams && in_ti.is_message) {
                // Wrap the typed message as an HRPC Request.
                out << "    logos::hrpc::Request rpc_req;\n";
                out << "    rpc_req.doc = req.doc;\n";
                out << "    rpc_req.map = rpc_req.doc.root_object().as_tiny_map();\n";
                uint16_t in_ch  = client_streams ? 1 : 0;
                uint16_t out_ch = server_streams ? 1 : 0;
                out << "    return session_.call(" << cst
                    << ", std::move(rpc_req), " << in_ch << ", " << out_ch << ");\n";
            } else {
                // Streaming or no-arg call: forward empty request.
                out << "    auto rpc_req = logos::hrpc::Request::make();\n";
                uint16_t in_ch  = client_streams ? 1 : 0;
                uint16_t out_ch = server_streams ? 1 : 0;
                out << "    return session_.call(" << cst
                    << ", std::move(rpc_req), " << in_ch << ", " << out_ch << ");\n";
            }
            out << "}\n\n";
        }
    }
}

// ---------------------------------------------------------------------------
// Signature helpers
// ---------------------------------------------------------------------------

std::string CodeGen::rpc_handler_sig(TinyMapView rpc_node) const {
    std::string method = str_field(rpc_node, NAME.code);

    AnyVal in_av = rpc_node.get(uint8_t(INPUT.code));
    TypeInfo in_ti;
    if (!in_av.is_null()) in_ti = resolve_type(map_of(in_av));

    std::string sig = method + "(";
    bool first = true;

    if (!in_ti.is_stream && in_ti.is_message) {
        sig += "const " + in_ti.name + "& req";
        first = false;
    }
    if (!first) sig += ", ";
    sig += "logos::hrpc::Context& ctx)";
    return sig;
}

std::string CodeGen::rpc_client_sig(TinyMapView rpc_node) const {
    std::string method = str_field(rpc_node, NAME.code);

    AnyVal in_av = rpc_node.get(uint8_t(INPUT.code));
    TypeInfo in_ti;
    if (!in_av.is_null()) in_ti = resolve_type(map_of(in_av));

    std::string sig = method + "(";
    if (!in_ti.is_stream && in_ti.is_message)
        sig += "const " + in_ti.name + "& req";
    sig += ")";
    return sig;
}

} // namespace logos::hrpc_gen
