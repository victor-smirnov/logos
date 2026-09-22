// Parser, type checker, rule planner and stratifier for the Souffle subset
// described in dl.hpp.
#include "dl_internal.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <functional>
#include <unordered_set>

namespace logos::dl {

// ── Symbols / Error ─────────────────────────────────────────────────────

Value Symbols::intern(std::string_view s) {
    if (auto it = ids_.find(s); it != ids_.end()) return it->second;
    store_.emplace_back(s);
    Value id = static_cast<Value>(store_.size() - 1);
    ids_.emplace(std::string_view(store_.back()), id);
    return id;
}

std::optional<Value> Symbols::find(std::string_view s) const {
    auto it = ids_.find(s);
    if (it == ids_.end()) return std::nullopt;
    return it->second;
}

std::string Error::str() const {
    if (file.empty()) return message;
    return std::format("{}:{}: {}", file, line, message);
}

std::optional<uint32_t> Program::relation(std::string_view name) const {
    auto it = rel_ids_.find(std::string(name));
    if (it == rel_ids_.end()) return std::nullopt;
    return it->second;
}

// ── Preprocessing: #include ─────────────────────────────────────────────
//
// The source is flattened into one text; `origins[i]` names where line i of
// the flattened text came from, so every later error points at the real file.

namespace {

struct Origin {
    std::string file;
    uint32_t    line = 0;
};

struct Flat {
    std::string         text;
    std::vector<Origin> origins;   // index = 0-based line of `text`
};

bool flatten(std::string_view text, std::string_view file,
             const IncludeResolver& resolver, std::vector<std::string>& stack,
             Flat& out, Error& err) {
    if (stack.size() > 32) {
        err = {std::string(file), 1, "#include nesting deeper than 32"};
        return false;
    }
    stack.emplace_back(file);
    uint32_t lineno = 0;
    size_t   pos    = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(
            pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        ++lineno;
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (line.substr(i).starts_with("#include")) {
            std::string_view rest = line.substr(i + 8);
            size_t q1 = rest.find('"');
            size_t q2 = q1 == std::string_view::npos ? q1 : rest.find('"', q1 + 1);
            if (q2 == std::string_view::npos) {
                err = {std::string(file), lineno, "malformed #include (expected \"name\")"};
                return false;
            }
            std::string name(rest.substr(q1 + 1, q2 - q1 - 1));
            if (std::find(stack.begin(), stack.end(), name) != stack.end()) {
                err = {std::string(file), lineno, std::format("#include cycle through '{}'", name)};
                return false;
            }
            std::optional<std::string> inc;
            if (resolver) inc = resolver(name);
            if (!inc) {
                err = {std::string(file), lineno, std::format("cannot resolve #include \"{}\"", name)};
                return false;
            }
            if (!flatten(*inc, name, resolver, stack, out, err)) return false;
        } else if (i < line.size() && line[i] == '#') {
            err = {std::string(file), lineno, "unsupported preprocessor directive (only #include)"};
            return false;
        } else {
            out.text.append(line);
            out.text.push_back('\n');
            out.origins.push_back({std::string(file), lineno});
        }
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }
    stack.pop_back();
    return true;
}

// ── Lexer ───────────────────────────────────────────────────────────────

enum class Tok : uint8_t {
    Ident, String, Number, Directive,
    LParen, RParen, Comma, Dot, ColonDash, Colon, Bang, Eq, Ne, SubType,
    End,
};

struct Token {
    Tok              kind = Tok::End;
    std::string_view text;
    uint32_t         line = 0;     // 0-based line in the flattened text
    size_t           offset = 0;
    std::string      str;          // unescaped String
};

class Lexer {
public:
    explicit Lexer(std::string_view s) : s_(s) {}

    bool next(Token& t, std::string& err) {
        skip_ws_();
        t = {};
        t.line   = line_;
        t.offset = p_;
        if (p_ >= s_.size()) { t.kind = Tok::End; return true; }
        char c = s_[p_];
        auto one = [&](Tok k, size_t n = 1) {
            t.kind = k; t.text = s_.substr(p_, n); p_ += n; return true;
        };
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t b = p_;
            while (p_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[p_])) ||
                                      s_[p_] == '_' || s_[p_] == '?'))
                ++p_;
            t.kind = Tok::Ident; t.text = s_.substr(b, p_ - b);
            return true;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '-' && p_ + 1 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_ + 1])))) {
            size_t b = p_++;
            while (p_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_]))) ++p_;
            t.kind = Tok::Number; t.text = s_.substr(b, p_ - b);
            return true;
        }
        if (c == '"') {
            size_t b = p_++;
            while (p_ < s_.size() && s_[p_] != '"') {
                if (s_[p_] == '\n') { err = "newline in string literal"; return false; }
                if (s_[p_] == '\\') {
                    if (p_ + 1 >= s_.size()) break;
                    char e = s_[p_ + 1];
                    if (e != '"' && e != '\\') {
                        err = std::format("unsupported escape '\\{}' in string literal", e);
                        return false;
                    }
                    t.str.push_back(e);
                    p_ += 2;
                    continue;
                }
                t.str.push_back(s_[p_++]);
            }
            if (p_ >= s_.size()) { err = "unterminated string literal"; return false; }
            ++p_;
            t.kind = Tok::String; t.text = s_.substr(b, p_ - b);
            return true;
        }
        if (c == '.' && p_ + 1 < s_.size() && std::isalpha(static_cast<unsigned char>(s_[p_ + 1]))) {
            size_t b = p_ + 1, e = b;
            while (e < s_.size() && std::isalpha(static_cast<unsigned char>(s_[e]))) ++e;
            std::string_view w = s_.substr(b, e - b);
            if (w == "decl" || w == "input" || w == "output" || w == "type") {
                t.kind = Tok::Directive; t.text = w; p_ = e;
                return true;
            }
        }
        switch (c) {
            case '(': return one(Tok::LParen);
            case ')': return one(Tok::RParen);
            case ',': return one(Tok::Comma);
            case '.': return one(Tok::Dot);
            case '=': return one(Tok::Eq);
            case ':':
                if (p_ + 1 < s_.size() && s_[p_ + 1] == '-') return one(Tok::ColonDash, 2);
                return one(Tok::Colon);
            case '!':
                if (p_ + 1 < s_.size() && s_[p_ + 1] == '=') return one(Tok::Ne, 2);
                return one(Tok::Bang);
            case '<':
                if (p_ + 1 < s_.size() && s_[p_ + 1] == ':') return one(Tok::SubType, 2);
                break;
        }
        err = std::format("unexpected character '{}'", c);
        return false;
    }

private:
    void skip_ws_() {
        while (p_ < s_.size()) {
            char c = s_[p_];
            if (c == '\n') { ++line_; ++p_; }
            else if (std::isspace(static_cast<unsigned char>(c))) ++p_;
            else if (c == '/' && p_ + 1 < s_.size() && s_[p_ + 1] == '/') {
                while (p_ < s_.size() && s_[p_] != '\n') ++p_;
            } else if (c == '/' && p_ + 1 < s_.size() && s_[p_ + 1] == '*') {
                p_ += 2;
                while (p_ + 1 < s_.size() && !(s_[p_] == '*' && s_[p_ + 1] == '/')) {
                    if (s_[p_] == '\n') ++line_;
                    ++p_;
                }
                p_ = std::min(p_ + 2, s_.size());
            } else break;
        }
    }

    std::string_view s_;
    size_t           p_    = 0;
    uint32_t         line_ = 0;
};

// ── Raw syntax, before relation names are resolved ──────────────────────

struct RawTerm {
    enum class Kind : uint8_t { Var, Str, Num, Wild } kind = Kind::Wild;
    std::string text;   // variable name or string constant
    int64_t     num = 0;
};

struct RawAtom {
    std::string          rel;
    std::vector<RawTerm> args;
    uint32_t             line = 0;
};

struct RawLit {
    Literal::Kind kind = Literal::Kind::Pos;
    RawAtom       atom;
    RawTerm       lhs, rhs;
};

struct RawClause {
    RawAtom             head;
    std::vector<RawLit> body;
    bool                is_fact = false;
    uint32_t            line = 0;
    std::string         text;
};

struct RawDecl {
    std::string                          name;
    std::vector<std::pair<std::string, std::string>> attrs;   // name, type
    uint32_t                             line = 0;
};

} // namespace

class Parser {
public:
    Parser(const Flat& flat, Symbols& syms) : flat_(flat), lex_(flat.text), syms_(syms) {}

    std::expected<Program, Error> run() {
        if (!advance_()) return std::unexpected(err_);
        while (tok_.kind != Tok::End) {
            bool ok = tok_.kind == Tok::Directive ? directive_() : clause_();
            if (!ok) return std::unexpected(err_);
        }
        Program p;
        if (!resolve_(p)) return std::unexpected(err_);
        if (!stratify_(p)) return std::unexpected(err_);
        return p;
    }

private:
    // ── token helpers ──
    bool advance_() {
        std::string e;
        if (!lex_.next(tok_, e)) return fail_(tok_.line, e);
        return true;
    }
    bool fail_(uint32_t line, std::string msg) {
        const Origin& o = line < flat_.origins.size() ? flat_.origins[line]
                          : (flat_.origins.empty() ? Origin{} : flat_.origins.back());
        err_ = {o.file, o.line, std::move(msg)};
        return false;
    }
    bool expect_(Tok k, const char* what) {
        if (tok_.kind != k)
            return fail_(tok_.line, std::format("expected {}, found '{}'", what, tok_.text));
        return advance_();
    }

    // ── directives ──
    bool directive_() {
        std::string_view d = tok_.text;
        uint32_t line = tok_.line;
        if (!advance_()) return false;
        if (d == "decl") {
            RawDecl rd;
            rd.line = line;
            if (tok_.kind != Tok::Ident) return fail_(tok_.line, "expected relation name after .decl");
            rd.name = std::string(tok_.text);
            if (!advance_() || !expect_(Tok::LParen, "'('")) return false;
            if (tok_.kind != Tok::RParen) {
                for (;;) {
                    if (tok_.kind != Tok::Ident) return fail_(tok_.line, "expected attribute name");
                    std::string an(tok_.text);
                    if (!advance_() || !expect_(Tok::Colon, "':'")) return false;
                    if (tok_.kind != Tok::Ident) return fail_(tok_.line, "expected attribute type");
                    rd.attrs.emplace_back(an, std::string(tok_.text));
                    if (!advance_()) return false;
                    if (tok_.kind == Tok::Comma) { if (!advance_()) return false; continue; }
                    break;
                }
            }
            if (!expect_(Tok::RParen, "')'")) return false;
            if (decls_.count(rd.name))
                return fail_(line, std::format("relation '{}' declared twice", rd.name));
            decl_order_.push_back(rd.name);
            decls_.emplace(rd.name, std::move(rd));
            return true;
        }
        if (d == "input" || d == "output") {
            for (;;) {
                if (tok_.kind != Tok::Ident) return fail_(tok_.line, std::format("expected relation name after .{}", d));
                (d == "input" ? inputs_ : outputs_).emplace_back(std::string(tok_.text), tok_.line);
                if (!advance_()) return false;
                if (tok_.kind == Tok::LParen)
                    return fail_(tok_.line, "IO parameters on .input/.output are not supported");
                if (tok_.kind == Tok::Comma) { if (!advance_()) return false; continue; }
                return true;
            }
        }
        // .type Name <: symbol|number
        if (tok_.kind != Tok::Ident) return fail_(tok_.line, "expected type name after .type");
        std::string tn(tok_.text);
        if (!advance_()) return false;
        if (tok_.kind != Tok::SubType)
            return fail_(tok_.line, "only '.type Name <: symbol|number' is supported");
        if (!advance_()) return false;
        std::optional<ColType> base = base_type_(tok_.text);
        if (tok_.kind != Tok::Ident || !base)
            return fail_(tok_.line, std::format("unknown base type '{}'", tok_.text));
        if (types_.count(tn)) return fail_(line, std::format("type '{}' declared twice", tn));
        types_[tn] = *base;
        return advance_();
    }

    std::optional<ColType> base_type_(std::string_view n) const {
        if (n == "symbol") return ColType::Symbol;
        if (n == "number") return ColType::Number;
        if (auto it = types_.find(std::string(n)); it != types_.end()) return it->second;
        return std::nullopt;
    }

    // ── clauses ──
    bool term_(RawTerm& t) {
        switch (tok_.kind) {
            case Tok::Ident:
                if (tok_.text == "_") t.kind = RawTerm::Kind::Wild;
                else { t.kind = RawTerm::Kind::Var; t.text = std::string(tok_.text); }
                return advance_();
            case Tok::String:
                t.kind = RawTerm::Kind::Str; t.text = tok_.str;
                return advance_();
            case Tok::Number: {
                t.kind = RawTerm::Kind::Num;
                auto [ptr, ec] = std::from_chars(tok_.text.data(), tok_.text.data() + tok_.text.size(), t.num);
                if (ec != std::errc{} || t.num < INT32_MIN || t.num > INT32_MAX)
                    return fail_(tok_.line, std::format("number '{}' out of 32-bit range", tok_.text));
                return advance_();
            }
            default:
                return fail_(tok_.line, std::format("expected a term, found '{}'", tok_.text));
        }
    }

    bool atom_(RawAtom& a) {
        a.line = tok_.line;
        a.rel  = std::string(tok_.text);
        if (!advance_() || !expect_(Tok::LParen, "'('")) return false;
        if (tok_.kind != Tok::RParen) {
            for (;;) {
                RawTerm t;
                if (!term_(t)) return false;
                a.args.push_back(std::move(t));
                if (tok_.kind == Tok::Comma) { if (!advance_()) return false; continue; }
                break;
            }
        }
        return expect_(Tok::RParen, "')'");
    }

    bool literal_(RawLit& l) {
        if (tok_.kind == Tok::Bang) {
            l.kind = Literal::Kind::Neg;
            if (!advance_()) return false;
            if (tok_.kind != Tok::Ident) return fail_(tok_.line, "expected an atom after '!'");
            return atom_(l.atom);
        }
        // An identifier followed by '(' is an atom; anything else starts a
        // comparison.
        if (tok_.kind == Tok::Ident && tok_.text != "_") {
            Lexer peek = lex_;
            Token nt; std::string e;
            if (peek.next(nt, e) && nt.kind == Tok::LParen) {
                l.kind = Literal::Kind::Pos;
                return atom_(l.atom);
            }
        }
        if (!term_(l.lhs)) return false;
        if (tok_.kind == Tok::Eq) l.kind = Literal::Kind::Eq;
        else if (tok_.kind == Tok::Ne) l.kind = Literal::Kind::Ne;
        else return fail_(tok_.line, std::format("expected '=' or '!=', found '{}'", tok_.text));
        if (!advance_()) return false;
        return term_(l.rhs);
    }

    bool clause_() {
        RawClause c;
        c.line = tok_.line;
        size_t start = tok_.offset;
        if (tok_.kind != Tok::Ident) return fail_(tok_.line, std::format("expected a rule or a directive, found '{}'", tok_.text));
        if (!atom_(c.head)) return false;
        if (tok_.kind == Tok::ColonDash) {
            if (!advance_()) return false;
            for (;;) {
                RawLit l;
                if (!literal_(l)) return false;
                c.body.push_back(std::move(l));
                if (tok_.kind == Tok::Comma) { if (!advance_()) return false; continue; }
                break;
            }
        } else {
            c.is_fact = true;
        }
        if (tok_.kind != Tok::Dot) return fail_(tok_.line, std::format("expected '.', found '{}'", tok_.text));
        c.text = std::string(flat_.text.substr(start, tok_.offset + 1 - start));
        if (!advance_()) return false;
        clauses_.push_back(std::move(c));
        return true;
    }

    // ── resolution and type checking ──
    bool resolve_(Program& p) {
        for (auto& n : decl_order_) {
            RawDecl& rd = decls_.at(n);
            RelDecl d;
            d.name = rd.name;
            const Origin& o = flat_.origins[rd.line];
            d.file = o.file;
            d.line = o.line;
            if (rd.attrs.size() > 31)
                return fail_(rd.line, std::format("relation '{}' has more than 31 columns", rd.name));
            for (auto& [an, at] : rd.attrs) {
                auto bt = base_type_(at);
                if (!bt) return fail_(rd.line, std::format("unknown type '{}' in relation '{}'", at, rd.name));
                d.attr_names.push_back(an);
                d.types.push_back(*bt);
            }
            p.rel_ids_.emplace(d.name, static_cast<uint32_t>(p.rels_.size()));
            p.rels_.push_back(std::move(d));
        }
        for (auto& [n, line] : inputs_) {
            auto r = p.relation(n);
            if (!r) return fail_(line, std::format(".input of undeclared relation '{}'", n));
            p.rels_[*r].input = true;
        }
        for (auto& [n, line] : outputs_) {
            auto r = p.relation(n);
            if (!r) return fail_(line, std::format(".output of undeclared relation '{}'", n));
            p.rels_[*r].output = true;
        }
        for (auto& c : clauses_)
            if (!clause_resolve_(p, c)) return false;
        return true;
    }

    bool const_(const RawTerm& rt, ColType want, uint32_t line, Value& out) {
        if (rt.kind == RawTerm::Kind::Str) {
            if (want != ColType::Symbol) return fail_(line, std::format("string \"{}\" in a number position", rt.text));
            out = syms_.intern(rt.text);
            return true;
        }
        if (want != ColType::Number) return fail_(line, std::format("number {} in a symbol position", rt.num));
        out = static_cast<Value>(static_cast<int32_t>(rt.num));
        return true;
    }

    bool clause_resolve_(Program& p, const RawClause& c) {
        Rule r;
        const Origin& o = flat_.origins[c.line];
        r.file = o.file;
        r.line = o.line;
        r.text = c.text;
        std::unordered_map<std::string, uint32_t> vars;
        std::vector<std::optional<ColType>>       vtype;

        auto term = [&](const RawTerm& rt, std::optional<ColType> want, uint32_t line,
                        Term& out) -> bool {
            switch (rt.kind) {
                case RawTerm::Kind::Wild:
                    out.kind = Term::Kind::Wild;
                    return true;
                case RawTerm::Kind::Var: {
                    auto [it, fresh] = vars.emplace(rt.text, static_cast<uint32_t>(r.var_names.size()));
                    if (fresh) { r.var_names.push_back(rt.text); vtype.push_back(std::nullopt); }
                    out.kind  = Term::Kind::Var;
                    out.value = it->second;
                    auto& vt = vtype[it->second];
                    if (want) {
                        if (vt && *vt != *want)
                            return fail_(line, std::format("variable {} used as both symbol and number", rt.text));
                        vt = want;
                    }
                    return true;
                }
                default: {
                    if (!want) return fail_(line, "cannot compare two constants");
                    out.kind = Term::Kind::Const;
                    return const_(rt, *want, line, out.value);
                }
            }
        };
        auto atom = [&](const RawAtom& ra, Atom& out, bool head) -> bool {
            auto rid = p.relation(ra.rel);
            if (!rid) return fail_(ra.line, std::format("undeclared relation '{}'", ra.rel));
            const RelDecl& d = p.rels_[*rid];
            if (ra.args.size() != d.types.size())
                return fail_(ra.line, std::format("relation '{}' has {} columns, used with {}",
                                                  ra.rel, d.types.size(), ra.args.size()));
            out.rel = *rid;
            for (size_t i = 0; i < ra.args.size(); ++i) {
                if (head && ra.args[i].kind == RawTerm::Kind::Wild)
                    return fail_(ra.line, "'_' in a rule head");
                Term t;
                if (!term(ra.args[i], d.types[i], ra.line, t)) return false;
                out.args.push_back(t);
            }
            return true;
        };

        if (c.is_fact) {
            Program::Fact f;
            auto rid = p.relation(c.head.rel);
            if (!rid) return fail_(c.head.line, std::format("undeclared relation '{}'", c.head.rel));
            const RelDecl& d = p.rels_[*rid];
            if (c.head.args.size() != d.types.size())
                return fail_(c.head.line, std::format("relation '{}' has {} columns, used with {}",
                                                      c.head.rel, d.types.size(), c.head.args.size()));
            f.rel = *rid;
            for (size_t i = 0; i < c.head.args.size(); ++i) {
                const RawTerm& rt = c.head.args[i];
                if (rt.kind == RawTerm::Kind::Var || rt.kind == RawTerm::Kind::Wild)
                    return fail_(c.head.line, "a fact must be ground (no variables)");
                Value v;
                if (!const_(rt, d.types[i], c.head.line, v)) return false;
                f.row.push_back(v);
            }
            p.facts_.push_back(std::move(f));
            return true;
        }

        // Body first so variable types are known from positive atoms.
        for (auto& rl : c.body) {
            Literal l;
            l.kind = rl.kind;
            if (rl.kind == Literal::Kind::Pos || rl.kind == Literal::Kind::Neg) {
                if (!atom(rl.atom, l.atom, false)) return false;
            } else {
                // A comparison takes its type from whichever side has one.
                auto side_type = [&](const RawTerm& t) -> std::optional<ColType> {
                    if (t.kind == RawTerm::Kind::Str) return ColType::Symbol;
                    if (t.kind == RawTerm::Kind::Num) return ColType::Number;
                    if (t.kind == RawTerm::Kind::Var)
                        if (auto it = vars.find(t.text); it != vars.end()) return vtype[it->second];
                    return std::nullopt;
                };
                if (rl.lhs.kind == RawTerm::Kind::Wild || rl.rhs.kind == RawTerm::Kind::Wild)
                    return fail_(c.line, "'_' in a comparison");
                std::optional<ColType> ty = side_type(rl.lhs);
                if (!ty) ty = side_type(rl.rhs);
                if (!ty) return fail_(c.line, "cannot infer the type of a comparison");
                if (!term(rl.lhs, ty, c.line, l.lhs)) return false;
                if (!term(rl.rhs, ty, c.line, l.rhs)) return false;
            }
            if (l.kind == Literal::Kind::Neg) p.has_negation_ = true;
            r.body.push_back(std::move(l));
        }
        if (!atom(c.head, r.head, true)) return false;

        detail::Plan plan;
        std::string  perr;
        if (!detail::plan_rule(r, plan, perr)) return fail_(c.line, perr);
        p.rules_.push_back(std::move(r));
        return true;
    }

    // ── stratification: Tarjan over head -> body edges ──
    bool stratify_(Program& p) {
        const uint32_t n = static_cast<uint32_t>(p.rels_.size());
        std::vector<std::vector<std::pair<uint32_t, bool>>> adj(n);   // (body rel, negated)
        for (auto& r : p.rules_)
            for (auto& l : r.body)
                if (l.kind == Literal::Kind::Pos || l.kind == Literal::Kind::Neg)
                    adj[r.head.rel].emplace_back(l.atom.rel, l.kind == Literal::Kind::Neg);

        std::vector<int32_t>  index(n, -1), low(n, 0), comp(n, -1);
        std::vector<bool>     on(n, false);
        std::vector<uint32_t> st;
        int32_t               counter = 0;
        std::function<void(uint32_t)> dfs = [&](uint32_t v) {
            index[v] = low[v] = counter++;
            st.push_back(v);
            on[v] = true;
            for (auto [w, neg] : adj[v]) {
                if (index[w] < 0) { dfs(w); low[v] = std::min(low[v], low[w]); }
                else if (on[w])   low[v] = std::min(low[v], index[w]);
            }
            if (low[v] == index[v]) {
                std::vector<uint32_t> scc;
                uint32_t w;
                do { w = st.back(); st.pop_back(); on[w] = false;
                     comp[w] = static_cast<int32_t>(p.strata_.size()); scc.push_back(w); } while (w != v);
                std::sort(scc.begin(), scc.end());
                p.strata_.push_back(std::move(scc));
            }
        };
        for (uint32_t v = 0; v < n; ++v)
            if (index[v] < 0) dfs(v);

        for (auto& r : p.rules_)
            for (auto& l : r.body)
                if (l.kind == Literal::Kind::Neg && comp[l.atom.rel] == comp[r.head.rel]) {
                    std::string cyc;
                    for (uint32_t x : p.strata_[comp[r.head.rel]])
                        cyc += (cyc.empty() ? "" : ", ") + p.rels_[x].name;
                    err_ = {r.file, r.line, std::format(
                        "negation of '{}' inside its own recursive component {{{}}}: not stratifiable",
                        p.rels_[l.atom.rel].name, cyc)};
                    return false;
                }
        return true;
    }

    const Flat& flat_;
    Lexer       lex_;
    Symbols&    syms_;
    Token       tok_;
    Error       err_;

    std::unordered_map<std::string, ColType>  types_;
    std::unordered_map<std::string, RawDecl>  decls_;
    std::vector<std::string>                  decl_order_;
    std::vector<std::pair<std::string, uint32_t>> inputs_, outputs_;
    std::vector<RawClause>                    clauses_;
};

std::expected<Program, Error>
Program::parse(std::string_view text, std::string_view file, Symbols& syms,
               const IncludeResolver& resolver) {
    Flat flat;
    Error err;
    std::vector<std::string> stack;
    if (!flatten(text, file, resolver, stack, flat, err)) return std::unexpected(err);
    Parser ps(flat, syms);
    return ps.run();
}

// ── Rule planning ───────────────────────────────────────────────────────

namespace detail {

bool plan_rule(const Rule& r, Plan& out, std::string& err, int32_t first_lit) {
    out = {};
    std::vector<bool> bound(r.var_names.size(), false);
    std::vector<bool> placed(r.body.size(), false);

    auto term_bound = [&](const Term& t) {
        return t.kind != Term::Kind::Var || bound[t.value];
    };
    auto atom_step = [&](uint32_t li, Step::Kind kind) {
        const Atom& a = r.body[li].atom;
        Step s;
        s.kind = kind;
        s.lit  = li;
        std::vector<bool> here(r.var_names.size(), false);
        for (uint32_t c = 0; c < a.args.size(); ++c) {
            const Term& t = a.args[c];
            ColOp op;
            op.col  = c;
            op.term = t;
            if (t.kind == Term::Kind::Wild) op.kind = ColOp::Kind::Wild;
            else if (t.kind == Term::Kind::Const || bound[t.value]) {
                op.kind = ColOp::Kind::Key;
                s.mask |= 1u << c;
            } else if (here[t.value]) op.kind = ColOp::Kind::Repeat;
            else { op.kind = ColOp::Kind::Bind; here[t.value] = true; }
            s.cols.push_back(op);
        }
        return s;
    };
    // Place every filter whose variables are now bound; bind `X = t` forms.
    auto place_ready = [&]() {
        for (bool progress = true; progress;) {
            progress = false;
            for (uint32_t li = 0; li < r.body.size(); ++li) {
                if (placed[li]) continue;
                const Literal& l = r.body[li];
                if (l.kind == Literal::Kind::Neg) {
                    bool ok = true;
                    for (auto& t : l.atom.args) ok = ok && term_bound(t);
                    if (!ok) continue;
                    out.steps.push_back(atom_step(li, Step::Kind::Neg));
                    placed[li] = progress = true;
                } else if (l.kind == Literal::Kind::Eq || l.kind == Literal::Kind::Ne) {
                    bool lb = term_bound(l.lhs), rb = term_bound(l.rhs);
                    if (lb && rb) {
                        Step s; s.kind = Step::Kind::Cmp; s.lit = li;
                        out.steps.push_back(s);
                        placed[li] = progress = true;
                    } else if (l.kind == Literal::Kind::Eq && (lb || rb)) {
                        Step s; s.kind = Step::Kind::Bind; s.lit = li;
                        const Term& var  = lb ? l.rhs : l.lhs;
                        s.var  = var.value;
                        s.from = lb ? l.lhs : l.rhs;
                        bound[s.var] = true;
                        out.steps.push_back(s);
                        placed[li] = progress = true;
                    }
                }
            }
        }
    };

    place_ready();
    auto place_pos = [&](uint32_t li) {
        out.steps.push_back(atom_step(li, Step::Kind::Scan));
        out.pos_lits.push_back(li);
        placed[li] = true;
        for (auto& t : r.body[li].atom.args)
            if (t.kind == Term::Kind::Var) bound[t.value] = true;
        place_ready();
    };
    if (first_lit >= 0 && static_cast<size_t>(first_lit) < r.body.size() &&
        r.body[first_lit].kind == Literal::Kind::Pos)
        place_pos(static_cast<uint32_t>(first_lit));
    for (uint32_t li = 0; li < r.body.size(); ++li) {
        if (r.body[li].kind != Literal::Kind::Pos || placed[li]) continue;
        place_pos(li);
    }
    for (uint32_t li = 0; li < r.body.size(); ++li) {
        if (placed[li]) continue;
        const Literal& l = r.body[li];
        auto unbound_of = [&](const Term& t) -> std::string {
            return t.kind == Term::Kind::Var && !bound[t.value] ? r.var_names[t.value] : "";
        };
        std::string v;
        if (l.kind == Literal::Kind::Neg) {
            for (auto& t : l.atom.args) if (v.empty()) v = unbound_of(t);
            err = std::format("variable {} in a negated atom is not bound by a positive atom", v);
        } else {
            v = unbound_of(l.lhs);
            if (v.empty()) v = unbound_of(l.rhs);
            err = std::format("variable {} in a comparison is not bound by a positive atom", v);
        }
        return false;
    }
    for (auto& t : r.head.args)
        if (t.kind == Term::Kind::Var && !bound[t.value]) {
            err = std::format("head variable {} is not bound by the body", r.var_names[t.value]);
            return false;
        }
    return true;
}

} // namespace detail
} // namespace logos::dl
