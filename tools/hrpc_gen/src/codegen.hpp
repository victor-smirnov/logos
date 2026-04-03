// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// hrpc_gen code generator: walks a parsed HRPC IDL AST and emits C++ stubs.

#pragma once

#include <logos/hermes/document.hpp>
#include <logos/hermes/view.hpp>

#include <iosfwd>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace logos::hrpc_gen {

// ---------------------------------------------------------------------------
// CodeGen — emits a .hpp and a .cpp from a parsed HRPC IDL AST.
//
// Usage:
//   CodeGen gen(ast_doc, "echo");   // package name used as C++ namespace
//   gen.emit_header(header_out, "ECHO_HRPC_GEN_HPP");
//   gen.emit_source(source_out, "echo.hrpc.gen.hpp");
// ---------------------------------------------------------------------------
class CodeGen {
public:
    // ast     — Hermes document returned by HrpcIdlParser::parse_file()
    // src_path — original .hrpc file path (for comment in generated files)
    CodeGen(logos::hermes::HermesCtr ast, std::string_view src_path);

    void emit_header(std::ostream& out, std::string_view header_guard) const;
    void emit_source(std::ostream& out, std::string_view include_name) const;

private:
    logos::hermes::HermesCtr ast_;
    std::string              src_path_;
    std::string              package_;     // "echo" or "foo.bar"
    std::string              cpp_ns_;      // "echo" or "foo::bar"

    // Top-level definitions collected in order.
    std::vector<logos::hermes::TinyMapView> items_;

    // Sets of user-defined names (to distinguish message vs enum refs).
    std::set<std::string> message_names_;
    std::set<std::string> enum_names_;

    // --- Initialisation ---
    void collect();

    // --- Low-level AST helpers ---

    // Read an arena string from an AnyVal offset.
    std::string str(logos::hermes::AnyVal av) const;

    // Read a TinyMapView from an AnyVal offset.
    logos::hermes::TinyMapView map_of(logos::hermes::AnyVal av) const;

    // Read an ArrayView from an AnyVal offset.
    logos::hermes::ArrayView arr_of(logos::hermes::AnyVal av) const;

    // Read CODE field (int32 value).
    int32_t code_of(logos::hermes::TinyMapView node) const;

    // Read a string field by raw key code.
    std::string str_field(logos::hermes::TinyMapView node, uint8_t key) const;

    // Read a qualified name (TYPE_REF with ITEMS=[ident_node, ...]) → "foo.Bar".
    std::string qualified_name(logos::hermes::TinyMapView type_ref_node) const;

    // Resolve a type_ref or stream_type node to a simple C++ type string.
    // Returns the type name for the getter (e.g. "uint32_t", "StringView", "PingRequest").
    struct TypeInfo {
        std::string name;        // resolved type name (simple, no package prefix)
        bool is_scalar  = false; // embedded in AnyVal value mode
        bool is_string  = false; // ArenaString pointer
        bool is_large   = false; // uint64/int64/float64 — not yet supported
        bool is_message = false; // user message type
        bool is_enum    = false; // user enum type
        bool is_map     = false; // map<K,V>
        bool is_stream  = false; // stream modifier (for rpc types)
        std::string cpp_get;     // C++ return type for getter (e.g. "uint32_t")
        std::string cpp_set;     // C++ param type for setter (e.g. "uint32_t")
        std::string scalar_cpp;  // underlying C++ scalar (e.g. "uint32_t")
    };
    TypeInfo resolve_type(logos::hermes::TinyMapView type_node) const;

    // --- Per-definition emitters ---

    // Header
    void emit_enum_decl    (std::ostream& out, logos::hermes::TinyMapView node) const;
    void emit_message_decl (std::ostream& out, logos::hermes::TinyMapView node) const;
    void emit_service_decl (std::ostream& out, logos::hermes::TinyMapView node) const;

    // Source
    void emit_message_impl (std::ostream& out, logos::hermes::TinyMapView node) const;
    void emit_service_impl (std::ostream& out, logos::hermes::TinyMapView node) const;

    // Per-field helpers
    void emit_field_getter_decl (std::ostream& out, logos::hermes::TinyMapView field) const;
    void emit_field_setter_decl (std::ostream& out, logos::hermes::TinyMapView field) const;
    void emit_field_getter_impl (std::ostream& out, std::string_view struct_name,
                                  logos::hermes::TinyMapView field) const;
    void emit_field_setter_impl (std::ostream& out, std::string_view struct_name,
                                  logos::hermes::TinyMapView field) const;

    // Service handler method signature (used in both decl and impl).
    std::string rpc_handler_sig(logos::hermes::TinyMapView rpc_node) const;
    std::string rpc_client_sig (logos::hermes::TinyMapView rpc_node) const;

    // Convert package "foo.bar" → C++ namespace "foo::bar".
    static std::string pkg_to_ns(std::string_view pkg);

    // Capitalise first letter of a name (for "EchoHandler" from "Echo").
    static std::string upper_first(std::string_view s);

    // Service endpoint constant name, e.g. "k_echo_ping_id".
    std::string endpoint_const(std::string_view service, std::string_view method) const;

    // Fully-qualified endpoint name used in endpoint_id_from_name().
    std::string endpoint_fqn(std::string_view service, std::string_view method) const;
};

} // namespace logos::hrpc_gen
