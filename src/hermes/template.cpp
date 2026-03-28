// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/template.hpp>
#include <logos/hermes/path.hpp>
#include <logos/hermes/stringify.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/tiny_object_map.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace logos::hermes {

// ============================================================================
// Template AST codes (stored in TinyObjectMap key 0)
// ============================================================================

namespace tpl_ast {
    inline constexpr uint8_t CODE              = 0;
    inline constexpr uint8_t EXPRESSION        = 1;
    inline constexpr uint8_t VARIABLE          = 2;
    inline constexpr uint8_t STATEMENTS        = 3;
    inline constexpr uint8_t ELSE_BRANCH       = 4;
    inline constexpr uint8_t STRIP_BEFORE      = 5;
    inline constexpr uint8_t STRIP_AFTER       = 6;

    inline constexpr int32_t TEXT_NODE    = 0;
    inline constexpr int32_t VAR_STMT     = 1;
    inline constexpr int32_t FOR_STMT     = 2;
    inline constexpr int32_t IF_STMT      = 3;
    inline constexpr int32_t ELSE_STMT    = 4;
    inline constexpr int32_t SET_STMT     = 5;
}

// ============================================================================
// Template Parser
// ============================================================================

class TemplateParser {
public:
    TemplateParser(std::string_view text, HermesCtr& doc)
        : text_(text), pos_(0), doc_(doc) {}

    void* parse() {
        auto* stmts = doc_.make_array();
        parse_block(stmts, "");
        return stmts;
    }

private:
    std::string_view text_;
    size_t pos_;
    HermesCtr& doc_;

    bool at_end() const { return pos_ >= text_.size(); }

    // Parse a block of text + statements until we hit an end tag or EOF.
    // end_tag: "endfor", "endif", "else", "elif", or "" for top-level.
    // Returns the keyword that terminated the block.
    std::string parse_block(ObjectArray* stmts, std::string_view end_tag) {
        while (!at_end()) {
            // Scan for {{ or {%
            size_t text_start = pos_;
            while (pos_ < text_.size()) {
                if (text_[pos_] == '{' && pos_ + 1 < text_.size()) {
                    char next = text_[pos_ + 1];
                    if (next == '{' || next == '%') break;
                }
                ++pos_;
            }

            // Emit text node if any.
            if (pos_ > text_start) {
                auto sv = text_.substr(text_start, pos_ - text_start);
                auto* s = doc_.make_string(sv);
                stmts->push_back(TaggedPtr{}, doc_.arena());
                stmts->slot(stmts->size() - 1)->set_pointer(s);
            }

            if (at_end()) break;

            if (text_[pos_ + 1] == '{') {
                // Variable output: {{ expr }}
                parse_var_stmt(stmts);
            } else {
                // Statement: {% ... %}
                std::string keyword = parse_statement(stmts, end_tag);
                if (!keyword.empty()) return keyword;
            }
        }
        return "";
    }

    void parse_var_stmt(ObjectArray* stmts) {
        pos_ += 2; // skip {{
        skip_ws();
        size_t expr_start = pos_;
        // Find closing }}
        while (pos_ + 1 < text_.size() && !(text_[pos_] == '}' && text_[pos_ + 1] == '}'))
            ++pos_;
        std::string_view expr_text = text_.substr(expr_start, pos_ - expr_start);
        // Trim trailing whitespace from expression.
        while (!expr_text.empty() && (expr_text.back() == ' ' || expr_text.back() == '\t'))
            expr_text.remove_suffix(1);

        if (pos_ + 1 < text_.size()) pos_ += 2; // skip }}

        // Parse the expression as HermesPath AST.
        auto expr_doc = parse_path(expr_text);

        auto* expr_str = doc_.make_string(expr_text);
        auto* node = doc_.make_tiny_map(4);
        node->put(tpl_ast::CODE, TaggedPtr::from_value(tpl_ast::VAR_STMT, type_hash::Integer), doc_.arena());
        node->put(tpl_ast::EXPRESSION, TaggedPtr{}, doc_.arena());
        node->slot(tpl_ast::EXPRESSION)->set_pointer(expr_str);

        stmts->push_back(TaggedPtr{}, doc_.arena());
        stmts->slot(stmts->size() - 1)->set_pointer(node);
    }

    // Returns non-empty string if this statement is a block-terminating keyword
    // (endfor, endif, else, elif).
    std::string parse_statement(ObjectArray* stmts, std::string_view end_tag) {
        pos_ += 2; // skip {%
        bool strip_before = false;
        if (pos_ < text_.size() && text_[pos_] == '-') { strip_before = true; ++pos_; }
        if (pos_ < text_.size() && text_[pos_] == '+') { ++pos_; } // preserve (default)

        skip_ws();
        std::string keyword = read_keyword();

        if (keyword == "endfor" || keyword == "endif") {
            skip_to_close_tag();
            return keyword;
        }
        if (keyword == "else") {
            skip_to_close_tag();
            return "else";
        }
        if (keyword == "elif") {
            // Don't consume — let the caller re-parse.
            // Back up to before the keyword.
            pos_ -= keyword.size();
            // Actually, return "elif" and let the if-handler deal with it.
            pos_ += keyword.size();
            // Read the elif expression.
            skip_ws();
            std::string_view elif_expr = read_until_close_tag();
            skip_to_close_tag();
            // Store elif expression for the caller.
            elif_expr_ = elif_expr;
            return "elif";
        }

        if (keyword == "for") {
            parse_for_stmt(stmts, strip_before);
            return "";
        }
        if (keyword == "if") {
            parse_if_stmt(stmts, strip_before);
            return "";
        }
        if (keyword == "set") {
            parse_set_stmt(stmts);
            return "";
        }

        // Unknown statement — skip.
        skip_to_close_tag();
        return "";
    }

    void parse_for_stmt(ObjectArray* stmts, bool /*strip*/) {
        skip_ws();
        std::string var_name = read_keyword();
        skip_ws();
        std::string in_kw = read_keyword();
        if (in_kw != "in") throw std::runtime_error("Template: expected 'in' in for statement");
        skip_ws();
        std::string_view expr_text = read_until_close_tag();
        skip_to_close_tag();

        // Parse body.
        auto* body = doc_.make_array();
        std::string end_kw = parse_block(body, "endfor");
        if (end_kw != "endfor") throw std::runtime_error("Template: expected 'endfor'");

        // Allocate strings first, then build the node.
        // All allocations happen before set_pointer to avoid any ordering issues.
        auto* var_str = doc_.make_string(var_name);
        auto* expr_str = doc_.make_string(expr_text);

        auto* node = doc_.make_tiny_map(8);
        node->put(tpl_ast::CODE, TaggedPtr::from_value(tpl_ast::FOR_STMT, type_hash::Integer), doc_.arena());
        node->put(tpl_ast::VARIABLE, TaggedPtr{}, doc_.arena());
        node->put(tpl_ast::EXPRESSION, TaggedPtr{}, doc_.arena());
        node->put(tpl_ast::STATEMENTS, TaggedPtr{}, doc_.arena());
        // Now set pointers — all allocations are done, no more arena mutations.
        node->slot(tpl_ast::VARIABLE)->set_pointer(var_str);
        node->slot(tpl_ast::EXPRESSION)->set_pointer(expr_str);
        node->slot(tpl_ast::STATEMENTS)->set_pointer(body);

        stmts->push_back(TaggedPtr{}, doc_.arena());
        stmts->slot(stmts->size() - 1)->set_pointer(node);
    }

    void parse_if_stmt(ObjectArray* stmts, bool /*strip*/) {
        skip_ws();
        std::string_view expr_text = read_until_close_tag();
        skip_to_close_tag();

        auto* body = doc_.make_array();
        std::string end_kw = parse_block(body, "endif");

        auto* expr_str = doc_.make_string(expr_text);
        auto* node = doc_.make_tiny_map(8);
        node->put(tpl_ast::CODE, TaggedPtr::from_value(tpl_ast::IF_STMT, type_hash::Integer), doc_.arena());
        node->put(tpl_ast::EXPRESSION, TaggedPtr{}, doc_.arena());
        node->put(tpl_ast::STATEMENTS, TaggedPtr{}, doc_.arena());
        node->slot(tpl_ast::EXPRESSION)->set_pointer(expr_str);
        node->slot(tpl_ast::STATEMENTS)->set_pointer(body);

        // Handle else/elif chain.
        if (end_kw == "else") {
            auto* else_body = doc_.make_array();
            parse_block(else_body, "endif");
            auto* else_node = doc_.make_tiny_map(4);
            else_node->put(tpl_ast::CODE, TaggedPtr::from_value(tpl_ast::ELSE_STMT, type_hash::Integer), doc_.arena());
            else_node->put(tpl_ast::STATEMENTS, TaggedPtr{}, doc_.arena());
            else_node->slot(tpl_ast::STATEMENTS)->set_pointer(else_body);
            node->put(tpl_ast::ELSE_BRANCH, TaggedPtr{}, doc_.arena());
            node->slot(tpl_ast::ELSE_BRANCH)->set_pointer(else_node);
        } else if (end_kw == "elif") {
            // Recursive: build a nested if from the elif.
            auto* elif_stmts = doc_.make_array();
            parse_if_stmt_with_expr(elif_stmts, elif_expr_);
            if (elif_stmts->size() > 0) {
                node->put(tpl_ast::ELSE_BRANCH, TaggedPtr{}, doc_.arena());
                node->slot(tpl_ast::ELSE_BRANCH)->set_pointer(
                    elif_stmts->slot(0)->as_ptr<void>());
            }
        }

        stmts->push_back(TaggedPtr{}, doc_.arena());
        stmts->slot(stmts->size() - 1)->set_pointer(node);
    }

    void parse_if_stmt_with_expr(ObjectArray* stmts, std::string_view expr_text) {
        auto* body = doc_.make_array();
        std::string end_kw = parse_block(body, "endif");

        auto* expr_str = doc_.make_string(expr_text);
        auto* node = doc_.make_tiny_map(8);
        node->put(tpl_ast::CODE, TaggedPtr::from_value(tpl_ast::IF_STMT, type_hash::Integer), doc_.arena());
        node->put(tpl_ast::EXPRESSION, TaggedPtr{}, doc_.arena());
        node->put(tpl_ast::STATEMENTS, TaggedPtr{}, doc_.arena());
        node->slot(tpl_ast::EXPRESSION)->set_pointer(expr_str);
        node->slot(tpl_ast::STATEMENTS)->set_pointer(body);

        if (end_kw == "else") {
            auto* else_body = doc_.make_array();
            parse_block(else_body, "endif");
            auto* else_node = doc_.make_tiny_map(4);
            else_node->put(tpl_ast::CODE, TaggedPtr::from_value(tpl_ast::ELSE_STMT, type_hash::Integer), doc_.arena());
            else_node->put(tpl_ast::STATEMENTS, TaggedPtr{}, doc_.arena());
            else_node->slot(tpl_ast::STATEMENTS)->set_pointer(else_body);
            node->put(tpl_ast::ELSE_BRANCH, TaggedPtr{}, doc_.arena());
            node->slot(tpl_ast::ELSE_BRANCH)->set_pointer(else_node);
        } else if (end_kw == "elif") {
            auto* elif_stmts = doc_.make_array();
            parse_if_stmt_with_expr(elif_stmts, elif_expr_);
            if (elif_stmts->size() > 0) {
                node->put(tpl_ast::ELSE_BRANCH, TaggedPtr{}, doc_.arena());
                node->slot(tpl_ast::ELSE_BRANCH)->set_pointer(
                    elif_stmts->slot(0)->as_ptr<void>());
            }
        }

        stmts->push_back(TaggedPtr{}, doc_.arena());
        stmts->slot(stmts->size() - 1)->set_pointer(node);
    }

    void parse_set_stmt(ObjectArray* stmts) {
        skip_ws();
        std::string var_name = read_keyword();
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '=') ++pos_;
        skip_ws();
        std::string_view expr_text = read_until_close_tag();
        skip_to_close_tag();

        auto* var_str = doc_.make_string(var_name);
        auto* expr_str = doc_.make_string(expr_text);
        auto* node = doc_.make_tiny_map(4);
        node->put(tpl_ast::CODE, TaggedPtr::from_value(tpl_ast::SET_STMT, type_hash::Integer), doc_.arena());
        node->put(tpl_ast::VARIABLE, TaggedPtr{}, doc_.arena());
        node->put(tpl_ast::EXPRESSION, TaggedPtr{}, doc_.arena());
        node->slot(tpl_ast::VARIABLE)->set_pointer(var_str);
        node->slot(tpl_ast::EXPRESSION)->set_pointer(expr_str);

        stmts->push_back(TaggedPtr{}, doc_.arena());
        stmts->slot(stmts->size() - 1)->set_pointer(node);
    }

    // --- Lexer helpers ---

    void skip_ws() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t')) ++pos_;
    }

    std::string read_keyword() {
        size_t start = pos_;
        while (pos_ < text_.size() && ((text_[pos_] >= 'a' && text_[pos_] <= 'z') ||
               (text_[pos_] >= 'A' && text_[pos_] <= 'Z') || text_[pos_] == '_' ||
               (text_[pos_] >= '0' && text_[pos_] <= '9')))
            ++pos_;
        return std::string(text_.substr(start, pos_ - start));
    }

    std::string_view read_until_close_tag() {
        size_t start = pos_;
        while (pos_ + 1 < text_.size()) {
            if ((text_[pos_] == '%' || text_[pos_] == '-' || text_[pos_] == '+') &&
                text_[pos_ + 1] == '}') break;
            if (text_[pos_] == '%' && text_[pos_ + 1] == '}') break;
            ++pos_;
        }
        size_t end = pos_;
        // Trim trailing whitespace.
        while (end > start && (text_[end - 1] == ' ' || text_[end - 1] == '\t')) --end;
        return text_.substr(start, end - start);
    }

    void skip_to_close_tag() {
        while (pos_ + 1 < text_.size()) {
            if (text_[pos_] == '%' && text_[pos_ + 1] == '}') { pos_ += 2; return; }
            if (text_[pos_] == '-' && text_[pos_ + 1] == '}') { pos_ += 2; return; }
            if (text_[pos_] == '+' && text_[pos_ + 1] == '}') { pos_ += 2; return; }
            ++pos_;
        }
    }

    std::string_view elif_expr_; // Temporary storage for elif expression.
};

// ============================================================================
// Template Renderer
// ============================================================================

class TemplateRenderer {
public:
    TemplateRenderer(const HermesCtr& data)
        : data_(data) {}

    std::string render(const ObjectArray* stmts) {
        visit_stmts(stmts);
        return std::move(out_);
    }

private:
    const HermesCtr& data_;
    std::string out_;
    HermesCtr scratch_ = HermesCtr::create(); // Scratch arena for materialized values.

    // Variable scope stack.
    struct VarBinding {
        std::string name;
        void* value;
    };
    std::vector<VarBinding> var_stack_;

    void push_var(const std::string& name, void* val) {
        var_stack_.push_back({name, val});
    }

    void pop_vars_to(size_t mark) {
        var_stack_.resize(mark);
    }

    void* find_var(std::string_view name) {
        // Search stack in reverse (most recent first).
        for (auto it = var_stack_.rbegin(); it != var_stack_.rend(); ++it) {
            if (it->name == name) return it->value;
        }
        return nullptr;
    }

    // Evaluate a HermesPath expression against current context.
    // Build a fresh context doc with data + template variables.
    HermesCtr build_context() {
        auto ctx_doc = HermesCtr::create();
        auto* ctx = ctx_doc.make_object_map();
        ctx_doc.set_root(ctx);

        // Copy data fields.
        void* data_root = const_cast<void*>(static_cast<const void*>(data_.header()->root.get()));
        if (data_root) {
            auto* db = static_cast<const uint8_t*>(data_root);
            TypeTag tag = TypeTag::read_before(db);
            if (tag.descriptor() == TagDescriptor::Map && tag.type_code() == type_hash::ObjectMap) {
                auto* data_map = static_cast<ObjectMap*>(data_root);
                data_map->for_each([&](ArenaString* key, TaggedPtr* val) {
                    if (val->is_value()) {
                        ctx->put(key->view(), *val, ctx_doc.arena());
                    } else if (val->is_pointer()) {
                        put_into_ctx(ctx, ctx_doc, key->view(), val->as_ptr<void>());
                    }
                });
            }
        }

        // Overlay template variables.
        for (auto& binding : var_stack_) {
            if (!binding.value) continue;
            put_into_ctx(ctx, ctx_doc, binding.name, binding.value);
        }

        return ctx_doc;
    }

    void* eval_expr(std::string_view expr_text) {
        auto ctx_doc = build_context();

        auto result = eval_path(ctx_doc, expr_text);
        if (!result.has_root()) return nullptr;
        return result.root<void>();
    }

    std::string eval_to_string(std::string_view expr_text) {
        auto ctx_doc = build_context();
        auto res = eval_path(ctx_doc, expr_text);
        if (!res.has_root()) return "";
        return value_to_string(res.root<void>());
    }

    std::string value_to_string(void* val) {
        if (!val) return "";
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        switch (tag.type_code()) {
            case type_hash::Varchar: return std::string(static_cast<const ArenaString*>(val)->view());
            case type_hash::Integer: return std::to_string(*static_cast<const int32_t*>(val));
            case type_hash::BigInt:  return std::to_string(*static_cast<const int64_t*>(val));
            case type_hash::Real: {
                char buf[32];
                int n = std::snprintf(buf, sizeof(buf), "%g", *static_cast<const float*>(val));
                return std::string(buf, n);
            }
            case type_hash::Double: {
                char buf[32];
                int n = std::snprintf(buf, sizeof(buf), "%g", *static_cast<const double*>(val));
                return std::string(buf, n);
            }
            case type_hash::Boolean:
                return *static_cast<const uint8_t*>(val) ? "true" : "false";
            default: return "";
        }
    }

    // Copy data root fields into a context ObjectMap.
    // Put an arena object into context map, copying if needed to avoid cross-arena pointers.
    // NOTE: ctx may be invalidated after this call (arena growth). Re-derive via offset.
    void put_into_ctx(ObjectMap* ctx, HermesCtr& ctx_doc, std::string_view key, void* val) {
        if (!val) { ctx->put(key, TaggedPtr{}, ctx_doc.arena()); return; }
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        uint64_t tc = tag.type_code();
        // Embeddable scalars: store as embedded TaggedPtr (no pointer needed).
        switch (tc) {
            case type_hash::Integer:
                ctx->put(key, TaggedPtr::from_value(*static_cast<const int32_t*>(val), tc), ctx_doc.arena());
                return;
            case type_hash::UInteger:
                ctx->put(key, TaggedPtr::from_value(*static_cast<const uint32_t*>(val), tc), ctx_doc.arena());
                return;
            case type_hash::Boolean:
                ctx->put(key, TaggedPtr::from_value(*static_cast<const uint8_t*>(val), tc), ctx_doc.arena());
                return;
            case type_hash::Real:
                ctx->put(key, TaggedPtr::from_value(*static_cast<const float*>(val), tc), ctx_doc.arena());
                return;
            case type_hash::SmallInt:
                ctx->put(key, TaggedPtr::from_value(*static_cast<const int16_t*>(val), tc), ctx_doc.arena());
                return;
            case type_hash::TinyInt:
                ctx->put(key, TaggedPtr::from_value(*static_cast<const int8_t*>(val), tc), ctx_doc.arena());
                return;
            case type_hash::Varchar: {
                auto* s = static_cast<const ArenaString*>(val);
                auto* copy = ArenaString::create(ctx_doc.arena(), s->view());
                ctx->put(key, TaggedPtr{}, ctx_doc.arena());
                ctx->get_slot(key)->set_pointer(copy);
                return;
            }
            default: {
                // Complex types (arrays, maps): store pointer directly.
                // This works only if the object lives long enough.
                ctx->put(key, TaggedPtr{}, ctx_doc.arena());
                ctx->get_slot(key)->set_pointer(val);
                return;
            }
        }
    }

    // Materialize an embedded TaggedPtr value into an arena-allocated object.
    // Uses a scratch doc so the pointer stays valid for the template's lifetime.
    void* materialize_embedded(TaggedPtr* slot) {
        if (!slot || slot->is_null()) return nullptr;
        uint8_t th = slot->value_type_hash();
        switch (th) {
            case type_hash::Integer:
                return scratch_.make_value<int32_t>(slot->as_value<int32_t>());
            case type_hash::UInteger:
                return scratch_.make_value<uint32_t>(slot->as_value<uint32_t>());
            case type_hash::Boolean: {
                TypeTag tag(type_hash::Boolean, TagDescriptor::Data);
                void* mem = scratch_.arena().allocate(1, 2, tag);
                *static_cast<uint8_t*>(mem) = slot->as_value<uint8_t>();
                return mem;
            }
            case type_hash::Real:
                return scratch_.make_value<float>(slot->as_value<float>());
            case type_hash::SmallInt:
                return scratch_.make_value<int16_t>(slot->as_value<int16_t>());
            case type_hash::TinyInt:
                return scratch_.make_value<int8_t>(slot->as_value<int8_t>());
            default: return nullptr;
        }
    }

    bool is_truthy(void* val) {
        if (!val) return false;
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        uint64_t tc = tag.type_code();
        if (tc == type_hash::Boolean) return *static_cast<const uint8_t*>(val) != 0;
        if (tc == type_hash::Integer) return *static_cast<const int32_t*>(val) != 0;
        if (tc == type_hash::Varchar) return static_cast<const ArenaString*>(val)->length() > 0;
        if (tag.descriptor() == TagDescriptor::Array) return static_cast<const ObjectArray*>(val)->size() > 0;
        return true;
    }

    // --- Visit methods ---

    void visit_stmts(const ObjectArray* stmts) {
        auto* stmts_mut = const_cast<ObjectArray*>(stmts);
        for (uint64_t i = 0; i < stmts->size(); ++i) {
            TaggedPtr* slot = stmts_mut->slot(i);
            if (slot->is_null()) continue;
            if (!slot->is_pointer()) continue;
            void* elem = slot->as_ptr<void>();
            auto* eb = static_cast<const uint8_t*>(elem);
            TypeTag tag = TypeTag::read_before(eb);

            if (tag.type_code() == type_hash::Varchar) {
                // Text node.
                out_ += static_cast<const ArenaString*>(elem)->view();
            } else if (tag.descriptor() == TagDescriptor::Map && tag.type_code() == type_hash::Hermes) {
                // Statement node (TinyObjectMap).
                auto* node = static_cast<TinyObjectMap*>(elem);
                int32_t code = node->get(tpl_ast::CODE).as_value<int32_t>();
                switch (code) {
                    case tpl_ast::VAR_STMT:  visit_var(node); break;
                    case tpl_ast::FOR_STMT:  visit_for(node); break;
                    case tpl_ast::IF_STMT:   visit_if(node); break;
                    case tpl_ast::SET_STMT:  visit_set(node); break;
                    default: break;
                }
            }
        }
    }

    void visit_var(TinyObjectMap* node) {
        auto* expr_str = node->slot(tpl_ast::EXPRESSION)->as_ptr<ArenaString>();
        out_ += eval_to_string(expr_str->view());
    }

    void visit_for(TinyObjectMap* node) {
        auto* var_str = node->slot(tpl_ast::VARIABLE)->as_ptr<ArenaString>();
        auto* expr_str = node->slot(tpl_ast::EXPRESSION)->as_ptr<ArenaString>();
        auto* body = node->slot(tpl_ast::STATEMENTS)->as_ptr<ObjectArray>();

        std::string var_name(var_str->view());

        // Evaluate the iterable expression.
        // This returns a document whose root should be an array.
        auto iter_result = eval_path_for_iterate(expr_str->view());
        if (!iter_result.has_root()) return;

        auto* arr = iter_result.root<ObjectArray>();
        if (!arr) return;

        size_t stack_mark = var_stack_.size();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            TaggedPtr* slot = const_cast<ObjectArray*>(arr)->slot(i);
            void* elem = nullptr;
            if (slot->is_pointer()) {
                elem = slot->as_ptr<void>();
            } else if (slot->is_value()) {
                // Materialize embedded value into a temporary arena object.
                elem = materialize_embedded(slot);
            }

            pop_vars_to(stack_mark);
            push_var(var_name, elem);
            visit_stmts(body);
        }
        pop_vars_to(stack_mark);
    }

    HermesCtr eval_path_for_iterate(std::string_view expr_text) {
        auto ctx_doc = build_context();
        return eval_path(ctx_doc, expr_text);
    }

    void visit_if(TinyObjectMap* node) {
        auto* expr_str = node->slot(tpl_ast::EXPRESSION)->as_ptr<ArenaString>();

        // Evaluate condition.
        void* cond = eval_expr(expr_str->view());
        if (is_truthy(cond)) {
            auto* body = node->slot(tpl_ast::STATEMENTS)->as_ptr<ObjectArray>();
            visit_stmts(body);
        } else if (node->has_key(tpl_ast::ELSE_BRANCH)) {
            auto* else_node = node->slot(tpl_ast::ELSE_BRANCH)->as_ptr<TinyObjectMap>();
            int32_t code = else_node->get(tpl_ast::CODE).as_value<int32_t>();
            if (code == tpl_ast::ELSE_STMT) {
                auto* body = else_node->slot(tpl_ast::STATEMENTS)->as_ptr<ObjectArray>();
                visit_stmts(body);
            } else if (code == tpl_ast::IF_STMT) {
                // elif chain — recursive.
                visit_if(else_node);
            }
        }
    }

    void visit_set(TinyObjectMap* node) {
        auto* var_str = node->slot(tpl_ast::VARIABLE)->as_ptr<ArenaString>();
        auto* expr_str = node->slot(tpl_ast::EXPRESSION)->as_ptr<ArenaString>();
        void* val = eval_expr(expr_str->view());
        push_var(std::string(var_str->view()), val);
    }
};

// ============================================================================
// Public API
// ============================================================================

HermesCtr parse_template(std::string_view tpl) {
    auto doc = HermesCtr::create();
    TemplateParser parser(tpl, doc);
    void* root = parser.parse();
    doc.set_root_offset(doc.offset_of(root));
    return doc;
}

std::string render_template(const HermesCtr& tpl, const HermesCtr& data) {
    auto* stmts = tpl.root<ObjectArray>();
    if (!stmts) return "";
    TemplateRenderer renderer(data);
    return renderer.render(stmts);
}

std::string render(std::string_view tpl_text, const HermesCtr& data) {
    auto tpl = parse_template(tpl_text);
    return render_template(tpl, data);
}

} // namespace logos::hermes
