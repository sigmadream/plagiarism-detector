#include "dna_pipeline.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "configuration_loader.hpp"

namespace cpptr::internal {

namespace {

constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

// A user-defined function is inlined at its call sites (program static tracing).
// These limits keep pathological call graphs from exploding the DNA length.
constexpr std::size_t max_inline_depth = 8;
constexpr std::size_t max_dna_events = 250000;

struct scan_result {
    std::vector<dna_event> events;
    std::optional<cpptr::dna_error> error;
};

bool is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool is_identifier_start(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

std::string trim(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\n\r\f\v");
    if (begin == std::string_view::npos) return "";
    const auto end = value.find_last_not_of(" \t\n\r\f\v");
    return std::string(value.substr(begin, end - begin + 1));
}

cpptr::dna_error make_error(
    cpptr::dna_error_code code,
    std::string message,
    std::filesystem::path path) {
    return cpptr::dna_error{
        code,
        std::move(message),
        std::move(path),
    };
}

std::optional<std::vector<std::string>> read_source_lines(
    const std::filesystem::path& source_path,
    cpptr::dna_error& error) {
    std::ifstream input(source_path);
    if (!input.is_open()) {
        error = make_error(
            cpptr::dna_error_code::preprocessing_failure,
            "source file could not be opened for DNA generation",
            source_path);
        return std::nullopt;
    }

    // Split on LF, CRLF and bare CR so that files saved with classic Mac line endings
    // are not collapsed into a single preprocessor line.
    const std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::vector<std::string> lines;
    std::string line;
    for (std::size_t i = 0; i < content.size(); ++i) {
        const char ch = content[i];
        if (ch == '\n') {
            lines.push_back(std::move(line));
            line.clear();
        } else if (ch == '\r') {
            lines.push_back(std::move(line));
            line.clear();
            if (i + 1 < content.size() && content[i + 1] == '\n') ++i;
        } else {
            line.push_back(ch);
        }
    }
    if (!line.empty()) lines.push_back(std::move(line));

    return lines;
}

std::optional<std::unordered_set<std::string>> load_keyword_names(
    const std::filesystem::path& keyword_path,
    cpptr::dna_error& error) {
    std::ifstream input(keyword_path);
    if (!input.is_open()) {
        error = make_error(
            cpptr::dna_error_code::config_load_failure,
            "keyword table could not be opened",
            keyword_path);
        return std::nullopt;
    }

    std::unordered_set<std::string> names;
    for (std::string line, name; std::getline(input, line);) {
        if (std::istringstream(trim(line)) >> name) names.insert(name);
    }

    if (names.empty()) {
        error = make_error(
            cpptr::dna_error_code::config_load_failure,
            "keyword table did not contain any token names",
            keyword_path);
        return std::nullopt;
    }
    return names;
}

std::string strip_literals_and_comments(std::string_view line, bool& in_block_comment) {
    std::string masked(line);
    bool in_string = false, in_char = false, escaped = false;
    for (std::size_t index = 0; index < masked.size(); ++index) {
        char& ch = masked[index];
        const char next = index + 1 < masked.size() ? masked[index + 1] : '\0';
        if (in_block_comment) {
            if (ch == '*' && next == '/') { in_block_comment = false; masked[index + 1] = ' '; ++index; }
            ch = ' '; continue;
        }
        if (in_string || in_char) {
            if (!escaped && ch == (in_string ? '"' : '\'')) in_string = in_char = false;
            escaped = !escaped && ch == '\\'; ch = ' '; continue;
        }
        escaped = false;
        if (ch == '/' && next == '/') { std::fill(masked.begin() + index, masked.end(), ' '); break; }
        if (ch == '/' && next == '*') { in_block_comment = true; ch = masked[index + 1] = ' '; ++index; continue; }
        if (ch == '"' || ch == '\'') { (ch == '"' ? in_string : in_char) = true; ch = ' '; continue; }
    }
    return masked;
}

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

enum class lex_kind { identifier, keyword, number, punct };

struct lex_token {
    lex_kind kind;
    std::string text;   // source spelling
    std::string dna;    // DNA token name; empty when the token has no DNA representation
    std::size_t line;
    std::size_t column;
};

// Keywords that map to a DNA token name in the keyword table.
const std::unordered_map<std::string_view, std::string_view>& keyword_dna_names() {
    static const std::unordered_map<std::string_view, std::string_view> table{
        {"void", "VOID"},         {"int", "INT"},           {"char", "CHAR"},
        {"bool", "BOOL"},         {"short", "SHORT"},       {"float", "FLOAT"},
        {"double", "DOUBLE"},     {"signed", "SIGNED"},     {"long", "LONG"},
        {"unsigned", "UNSIGNED"}, {"do", "DO"},             {"true", "TRUE"},
        {"false", "FALSE"},       {"struct", "STRUCT"},     {"static", "STATIC"},
        {"continue", "CONTINUE"}, {"switch", "SWITCH"},     {"case", "CASE"},
        {"default", "DEFAULT"},   {"break", "BREAK"},       {"return", "RETURN"},
        {"goto", "GOTO"},         {"NULL", "NULL"},         {"nullptr", "NULL"},
        {"typedef", "TYPEDEF"},   {"sizeof", "SIZEOF"},     {"const", "CONST"},
        {"volatile", "VOLATILE"}, {"inline", "INLINE"},     {"public", "PUBLIC"},
        {"private", "PRIVATE"},   {"friend", "FRIEND"},     {"protected", "PROTECTED"},
        {"new", "NEW"},           {"delete", "DELETE"},     {"virtual", "VIRTUAL"},
        {"try", "TRY"},           {"catch", "CATCH"},       {"throw", "THROW"},
        {"class", "CLASS"},       {"namespace", "NAMESPACE"}, {"if", "IF"},
        {"else", "ELSE"},         {"for", "FOR"},           {"while", "WHILE"},
    };
    return table;
}

// Reserved words without a DNA token. They are never treated as function names.
const std::unordered_set<std::string_view>& reserved_words() {
    static const std::unordered_set<std::string_view> table{
        "auto", "register", "extern", "enum", "union", "template", "typename", "using",
        "operator", "this", "static_cast", "dynamic_cast", "reinterpret_cast", "const_cast",
        "decltype", "alignof", "alignas", "noexcept", "constexpr", "consteval", "constinit",
        "explicit", "mutable", "static_assert", "thread_local", "export", "import", "module",
        "co_await", "co_return", "co_yield", "concept", "requires", "wchar_t", "char8_t",
        "char16_t", "char32_t", "asm", "typeid", "and", "or", "not", "xor", "bitand", "bitor",
        "compl", "and_eq", "or_eq", "xor_eq", "not_eq", "override", "final",
    };
    return table;
}

// Keywords that can precede a declared name. Used to tell `int f(int);` from `f(x);`.
bool is_type_keyword(std::string_view text) {
    static const std::unordered_set<std::string_view> table{
        "void", "int", "char", "bool", "short", "float", "double", "signed", "long", "unsigned",
        "const", "volatile", "static", "inline", "struct", "class", "union", "enum", "virtual",
        "friend", "typedef", "extern", "auto", "constexpr", "explicit", "wchar_t", "char8_t",
        "char16_t", "char32_t", "register", "mutable", "typename",
    };
    return table.contains(text);
}

// Qualifiers that may follow a parameter list before the body or the semicolon.
bool is_trailing_qualifier(std::string_view text) {
    static const std::unordered_set<std::string_view> table{
        "const", "volatile", "noexcept", "override", "final", "throw", "constexpr", "mutable",
        "&", "&&",
    };
    return table.contains(text);
}

struct operator_spelling {
    std::string_view text;
    std::string_view dna;
};

// Longest spellings first so that `<<=` wins over `<<` and `<`.
constexpr operator_spelling operators[] = {
    {"<<=", "SHL_EQ"},   {">>=", "SHR_EQ"},   {"->*", ""},         {"...", ""},
    {"<<", "SHL"},       {">>", "SHR"},       {"<=", "LT_EQ"},     {">=", "GT_EQ"},
    {"==", "EQ"},        {"!=", "NOT_EQ"},    {"&&", "AND"},       {"||", "OR"},
    {"++", "INCR"},      {"--", "DECR"},      {"+=", "PLUS_EQ"},   {"-=", "MINUS_EQ"},
    {"*=", "TIMES_EQ"},  {"/=", "DIV_EQ"},    {"%=", "MOD_EQ"},    {"&=", "BITAND_EQ"},
    {"^=", "BITXOR_EQ"}, {"|=", "BITOR_EQ"},  {"->", ""},          {"::", ""},
    {".*", ""},          {"<", "LT"},         {">", "GT"},         {"+", "PLUS"},
    {"-", "MINUS"},      {"*", "MULTIPLE"},   {"/", "DIVIDE"},     {"%", "MOD"},
    {"&", "BIT_AND"},    {"|", "BIT_OR"},     {"^", "BIT_XOR"},    {"!", "NOT"},
    {"=", "ASSIGN_EQ"},  {"{", "BLOCK_START"}, {"}", "BLOCK_END"},
};

void lex_line(std::string_view line, std::size_t out_line, std::vector<lex_token>& tokens) {
    std::size_t i = 0;
    while (i < line.size()) {
        const char ch = line[i];
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) { ++i; continue; }

        if (is_identifier_start(ch)) {
            std::size_t j = i;
            while (j < line.size() && is_identifier_char(line[j])) ++j;
            std::string text(line.substr(i, j - i));
            const auto& keywords = keyword_dna_names();
            if (const auto it = keywords.find(text); it != keywords.end()) {
                tokens.push_back({lex_kind::keyword, std::move(text), std::string(it->second), out_line, i});
            } else if (reserved_words().contains(text)) {
                tokens.push_back({lex_kind::keyword, std::move(text), "", out_line, i});
            } else {
                tokens.push_back({lex_kind::identifier, std::move(text), "", out_line, i});
            }
            i = j;
            continue;
        }

        const bool digit = std::isdigit(static_cast<unsigned char>(ch)) != 0;
        const bool leading_dot = ch == '.' && i + 1 < line.size() &&
                                 std::isdigit(static_cast<unsigned char>(line[i + 1])) != 0;
        if (digit || leading_dot) {
            std::size_t j = i + 1;
            while (j < line.size()) {
                const char c = line[j];
                if (is_identifier_char(c) || c == '.') { ++j; continue; }
                if ((c == '+' || c == '-') && (line[j - 1] == 'e' || line[j - 1] == 'E')) { ++j; continue; }
                break;
            }
            tokens.push_back({lex_kind::number, std::string(line.substr(i, j - i)), "", out_line, i});
            i = j;
            continue;
        }

        bool matched = false;
        for (const auto& op : operators) {
            if (line.substr(i).starts_with(op.text)) {
                tokens.push_back({lex_kind::punct, std::string(op.text), std::string(op.dna), out_line, i});
                i += op.text.size();
                matched = true;
                break;
            }
        }
        if (!matched) {
            tokens.push_back({lex_kind::punct, std::string(1, ch), "", out_line, i});
            ++i;
        }
    }
}

// ---------------------------------------------------------------------------
// Structural analysis: bracket matching, function definitions, prototypes
// ---------------------------------------------------------------------------

struct function_def {
    std::string name;
    std::size_t span_begin;    // first token of the declaration (return type)
    std::size_t name_index;
    std::size_t params_open;   // index of '('
    std::size_t params_close;  // index of matching ')'
    std::size_t body_open;     // index of '{'
    std::size_t body_close;    // index of matching '}'
    bool root = false;         // emitted in place instead of only at call sites
};

struct prototype {
    std::size_t span_begin;
    std::size_t end; // index of the terminating ';'
};

struct source_analysis {
    std::vector<lex_token> tokens;
    std::vector<std::size_t> match;           // matching bracket index or npos
    std::vector<std::size_t> enclosing_brace; // innermost enclosing '{' or npos
    std::vector<bool> class_brace;            // true when the '{' opens a class/struct/union body
    std::vector<function_def> functions;
    std::vector<prototype> prototypes;
    std::unordered_map<std::size_t, std::size_t> function_by_span_begin;
    std::unordered_map<std::size_t, std::size_t> prototype_by_span_begin;
    std::unordered_set<std::size_t> declared_name_indices;
    std::unordered_map<std::string, std::size_t> function_by_name; // first definition wins

    [[nodiscard]] const function_def* function_at(std::size_t index) const {
        const auto it = function_by_span_begin.find(index);
        return it == function_by_span_begin.end() ? nullptr : &functions[it->second];
    }

    [[nodiscard]] const prototype* prototype_at(std::size_t index) const {
        const auto it = prototype_by_span_begin.find(index);
        return it == prototype_by_span_begin.end() ? nullptr : &prototypes[it->second];
    }

    [[nodiscard]] const function_def* find_function(const std::string& name) const {
        const auto it = function_by_name.find(name);
        return it == function_by_name.end() ? nullptr : &functions[it->second];
    }

    // identifier immediately followed by '(' that is neither a definition nor a prototype name
    [[nodiscard]] bool is_call_site(std::size_t index) const {
        return tokens[index].kind == lex_kind::identifier &&
               index + 1 < tokens.size() && tokens[index + 1].text == "(" &&
               !declared_name_indices.contains(index);
    }
};

void match_brackets(source_analysis& a) {
    const auto n = a.tokens.size();
    a.match.assign(n, npos);
    a.enclosing_brace.assign(n, npos);
    a.class_brace.assign(n, false);
    std::vector<std::size_t> parens, braces;
    for (std::size_t i = 0; i < n; ++i) {
        a.enclosing_brace[i] = braces.empty() ? npos : braces.back();
        const auto& text = a.tokens[i].text;
        if (text == "(") {
            parens.push_back(i);
        } else if (text == ")") {
            if (!parens.empty()) { a.match[i] = parens.back(); a.match[parens.back()] = i; parens.pop_back(); }
        } else if (text == "{") {
            braces.push_back(i);
        } else if (text == "}") {
            if (!braces.empty()) { a.match[i] = braces.back(); a.match[braces.back()] = i; braces.pop_back(); }
        }
    }

    // A '{' opens a class body when walking back over the class head reaches class/struct/union.
    for (std::size_t i = 0; i < n; ++i) {
        if (a.tokens[i].text != "{") continue;
        for (std::size_t k = i; k-- > 0;) {
            const auto& t = a.tokens[k];
            if (t.text == "class" || t.text == "struct" || t.text == "union") { a.class_brace[i] = true; break; }
            const bool head_token = t.kind == lex_kind::identifier || t.kind == lex_kind::keyword ||
                                    t.text == ":" || t.text == "," || t.text == "::" ||
                                    t.text == "<" || t.text == ">";
            if (!head_token) break;
        }
    }
}

bool in_class_scope(const source_analysis& a, std::size_t index) {
    const auto brace = a.enclosing_brace[index];
    return brace != npos && a.class_brace[brace];
}

// Walk back from the declared name over the tokens that can form its return type.
// Non-type keywords such as `return` or `else` end the walk: they cannot start a declaration.
std::size_t declaration_span_begin(const source_analysis& a, std::size_t name_index) {
    std::size_t k = name_index;
    while (k > 0) {
        const auto& p = a.tokens[k - 1];
        const bool type_token = p.kind == lex_kind::identifier ||
                                (p.kind == lex_kind::keyword && is_type_keyword(p.text)) ||
                                p.text == "*" || p.text == "&" || p.text == "&&" || p.text == "::" ||
                                p.text == "<" || p.text == ">";
        if (!type_token) break;
        --k;
    }
    return k;
}

// A declaration can only start at the beginning of the file, after a statement or block
// boundary, or after an access specifier. `return n * f(x);` and `case 1: f(x);` are calls.
bool can_start_declaration(const source_analysis& a, std::size_t span_begin) {
    if (span_begin == 0) return true;
    const auto& p = a.tokens[span_begin - 1];
    if (p.text == ";" || p.text == "{" || p.text == "}") return true;
    if (p.text == ":" && span_begin >= 2) {
        const auto& access = a.tokens[span_begin - 2].text;
        return access == "public" || access == "private" || access == "protected";
    }
    return false;
}

bool preceded_by_type(const source_analysis& a, std::size_t name_index) {
    std::size_t k = name_index;
    // skip `Ns::` qualifiers
    while (k >= 2 && a.tokens[k - 1].text == "::" && a.tokens[k - 2].kind == lex_kind::identifier) k -= 2;
    if (k == 0) return false;
    const auto& p = a.tokens[k - 1];
    if (p.kind == lex_kind::identifier) return true;
    if (p.kind == lex_kind::keyword) return is_type_keyword(p.text);
    return p.text == "*" || p.text == "&" || p.text == ">";
}

// Given the ')' of a parameter list, find the '{' that opens a function body.
std::optional<std::size_t> find_body_open(const source_analysis& a, std::size_t params_close) {
    const auto& tokens = a.tokens;
    std::size_t k = params_close + 1;
    bool trailing_return = false;
    while (k < tokens.size()) {
        const auto& t = tokens[k];
        if (t.text == "{") return k;
        if (t.text == "->") { trailing_return = true; ++k; continue; }
        if (trailing_return && (t.kind == lex_kind::identifier || t.kind == lex_kind::keyword ||
                                t.text == "::" || t.text == "*" || t.text == "&")) { ++k; continue; }
        if (is_trailing_qualifier(t.text)) { ++k; continue; }
        if (t.text == "(") { if (a.match[k] == npos) return std::nullopt; k = a.match[k] + 1; continue; }
        if (t.text != ":") return std::nullopt;

        // constructor initializer list: `: a(1), b{2} {`
        ++k;
        std::size_t paren_depth = 0;
        while (k < tokens.size()) {
            const auto& u = tokens[k];
            if (u.text == "(") {
                ++paren_depth;
            } else if (u.text == ")") {
                if (paren_depth == 0) return std::nullopt;
                --paren_depth;
            } else if (paren_depth == 0) {
                if (u.text == ";" || u.text == "}") return std::nullopt;
                if (u.text == "{") {
                    const auto& prev = tokens[k - 1];
                    if (prev.text == ")" || prev.text == "}") return k;
                    if (a.match[k] == npos) return std::nullopt;
                    k = a.match[k]; // skip brace initializer
                }
            }
            ++k;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// Given the ')' of a parameter list, find the ';' that ends a declaration, if any.
std::optional<std::size_t> declaration_end(const source_analysis& a, std::size_t params_close) {
    const auto& tokens = a.tokens;
    std::size_t k = params_close + 1;
    while (k < tokens.size()) {
        const auto& t = tokens[k];
        if (t.text == ";") return k;
        if (is_trailing_qualifier(t.text)) { ++k; continue; }
        if (t.text == "(") { if (a.match[k] == npos) return std::nullopt; k = a.match[k] + 1; continue; }
        if (t.text == "=") { // `= 0;`, `= default;`, `= delete;`
            for (++k; k < tokens.size(); ++k) {
                if (tokens[k].text == ";") return k;
                if (tokens[k].text == "{" || tokens[k].text == "}" || tokens[k].text == "(") return std::nullopt;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

void collect_declarations(source_analysis& a) {
    const auto& tokens = a.tokens;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].kind != lex_kind::identifier || tokens[i + 1].text != "(") continue;
        const auto params_open = i + 1;
        const auto params_close = a.match[params_open];
        if (params_close == npos) continue;

        if (const auto body_open = find_body_open(a, params_close)) {
            const auto body_close = a.match[*body_open];
            if (body_close == npos) continue;
            function_def def{tokens[i].text, declaration_span_begin(a, i), i, params_open, params_close, *body_open, body_close};
            a.function_by_span_begin[def.span_begin] = a.functions.size();
            a.function_by_name.try_emplace(def.name, a.functions.size());
            a.declared_name_indices.insert(i);
            a.functions.push_back(std::move(def));
            continue;
        }

        if (!preceded_by_type(a, i) && !in_class_scope(a, i)) continue;
        const auto span_begin = declaration_span_begin(a, i);
        if (!can_start_declaration(a, span_begin)) continue;
        if (const auto end = declaration_end(a, params_close)) {
            prototype proto{span_begin, *end};
            a.prototype_by_span_begin[proto.span_begin] = a.prototypes.size();
            a.declared_name_indices.insert(i);
            a.prototypes.push_back(proto);
        }
    }
}

// A function is a root when nothing in the file calls it, or when it is unreachable from the
// other roots (for example a recursive island). Roots are emitted in place; every other
// function only appears inlined at its call sites.
void mark_roots(source_analysis& a) {
    std::unordered_set<std::string> called;
    for (std::size_t i = 0; i < a.tokens.size(); ++i) {
        if (a.is_call_site(i)) called.insert(a.tokens[i].text);
    }

    std::vector<std::vector<std::size_t>> edges(a.functions.size());
    for (std::size_t f = 0; f < a.functions.size(); ++f) {
        const auto& def = a.functions[f];
        for (std::size_t i = def.params_open; i <= def.body_close; ++i) {
            if (!a.is_call_site(i)) continue;
            if (const auto it = a.function_by_name.find(a.tokens[i].text); it != a.function_by_name.end()) {
                edges[f].push_back(it->second);
            }
        }
    }

    std::vector<bool> reachable(a.functions.size(), false);
    const auto visit = [&](std::size_t start) {
        std::vector<std::size_t> stack{start};
        while (!stack.empty()) {
            const auto f = stack.back();
            stack.pop_back();
            if (reachable[f]) continue;
            reachable[f] = true;
            for (const auto g : edges[f]) if (!reachable[g]) stack.push_back(g);
        }
    };

    for (std::size_t f = 0; f < a.functions.size(); ++f) {
        if (!called.contains(a.functions[f].name)) { a.functions[f].root = true; visit(f); }
    }
    for (std::size_t f = 0; f < a.functions.size(); ++f) {
        if (!reachable[f]) { a.functions[f].root = true; visit(f); }
    }
}

// ---------------------------------------------------------------------------
// DNA emission with static tracing of user-defined function calls
// ---------------------------------------------------------------------------

class dna_emitter {
public:
    dna_emitter(const source_analysis& analysis, const std::unordered_set<std::string>& keyword_names)
        : analysis_(analysis), keyword_names_(keyword_names) {}

    std::vector<dna_event> run() {
        emit_range(0, analysis_.tokens.size(), 0, nullptr);
        return std::move(events_);
    }

private:
    void emit(std::string_view dna, const lex_token& at) {
        if (dna.empty() || !keyword_names_.contains(std::string(dna))) return;
        events_.emplace_back(std::string(dna), at.line, at.column);
    }

    void emit_function(const function_def& f, std::size_t depth) {
        emit_range(f.span_begin, f.params_open, depth, &f);
        emit_range(f.params_open, f.params_close + 1, depth, &f);
        emit_range(f.body_open, f.body_close + 1, depth, &f);
    }

    void inline_callee(const function_def& f, std::size_t depth, const lex_token& at) {
        const bool on_stack = std::find(call_stack_.begin(), call_stack_.end(), f.name) != call_stack_.end();
        if (depth < max_inline_depth && events_.size() < max_dna_events && !on_stack) {
            call_stack_.push_back(f.name);
            emit_function(f, depth + 1);
            call_stack_.pop_back();
        }
        emit("FUNC_END", at);
    }

    void emit_range(std::size_t begin, std::size_t end, std::size_t depth, const function_def* self) {
        const auto& tokens = analysis_.tokens;
        std::unordered_map<std::size_t, const function_def*> pending; // call ')' index -> callee
        for (std::size_t i = begin; i < end;) {
            if (const auto* def = analysis_.function_at(i); def != nullptr && def != self) {
                if (def->root) emit_function(*def, depth);
                i = def->body_close + 1;
                continue;
            }
            if (const auto* proto = analysis_.prototype_at(i)) {
                i = proto->end + 1;
                continue;
            }

            const auto& tok = tokens[i];
            if (analysis_.is_call_site(i)) {
                if (const auto* callee = analysis_.find_function(tok.text)) {
                    emit("FUNC_CALL", tok);
                    const auto close = analysis_.match[i + 1];
                    if (close != npos && close < end) pending[close] = callee;
                    else inline_callee(*callee, depth, tok);
                } else {
                    emit("UNTRACKABLE_FUNC_CALL", tok);
                }
                ++i;
                continue;
            }

            if (tok.text == ")") {
                if (const auto it = pending.find(i); it != pending.end()) {
                    const auto* callee = it->second;
                    pending.erase(it);
                    inline_callee(*callee, depth, tok);
                }
                ++i;
                continue;
            }

            emit(tok.dna, tok);
            ++i;
        }
    }

    const source_analysis& analysis_;
    const std::unordered_set<std::string>& keyword_names_;
    std::vector<dna_event> events_;
    std::vector<std::string> call_stack_;
};

scan_result scan_source(
    const std::vector<std::string>& lines,
    const std::unordered_set<std::string>& keyword_names,
    const std::filesystem::path& source_path) {
    scan_result result;
    auto it = std::find_if(lines.begin(), lines.end(), [](const auto& l) {
        auto c = trim(l); return !c.empty() && c[0] != '#';
    });
    const auto base = it != lines.end() ? static_cast<std::size_t>(std::distance(lines.begin(), it)) : 0;
    bool in_comment = false;

    source_analysis analysis;
    for (std::size_t i = base; i < lines.size(); ++i) {
        const auto trimmed = trim(lines[i]);
        auto masked = strip_literals_and_comments(lines[i], in_comment);
        if (!trimmed.empty() && trimmed[0] == '#') continue;
        lex_line(masked, i - base, analysis.tokens);
    }

    match_brackets(analysis);
    collect_declarations(analysis);
    mark_roots(analysis);
    result.events = dna_emitter(analysis, keyword_names).run();

    if (result.events.empty()) {
        result.error = make_error(
            cpptr::dna_error_code::parse_failure,
            "no DNA tokens were extracted from the source file",
            source_path);
    }

    return result;
}

cpptr::dna_result write_output(
    const std::filesystem::path& source_path,
    const std::filesystem::path& dna_directory,
    const std::vector<dna_event>& events) {
    std::error_code ec; std::filesystem::create_directories(dna_directory, ec);
    if (ec) return cpptr::dna_result::failure(make_error(cpptr::dna_error_code::write_failure, "failed to create DNA output directory", dna_directory));
    const auto out_path = (dna_directory / (source_path.filename().string() + ".DNA")).lexically_normal();
    std::ofstream out(out_path, std::ios::binary);
    if (!out) return cpptr::dna_result::failure(make_error(cpptr::dna_error_code::write_failure, "failed to open DNA output file", out_path));
    for (const auto& e : events) out << e.token() << '\t' << e.line() << '\t' << e.column() << '\n';
    if (!out) return cpptr::dna_result::failure(make_error(cpptr::dna_error_code::write_failure, "failed while writing DNA output file", out_path));

    const auto snapshot_path = std::filesystem::path(out_path.string() + ".src");
    std::filesystem::copy_file(source_path, snapshot_path, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return cpptr::dna_result::failure(make_error(cpptr::dna_error_code::write_failure, "failed to preserve source snapshot", snapshot_path));

    return cpptr::dna_result::success(out_path);
}

class default_file_system_port : public file_system_port {
public:
    std::optional<std::vector<std::string>> read_source_lines(const std::filesystem::path& source_path, cpptr::dna_error& error) override {
        return cpptr::internal::read_source_lines(source_path, error);
    }

    std::optional<std::unordered_set<std::string>> load_keyword_names(const std::filesystem::path& keyword_path, cpptr::dna_error& error) override {
        return cpptr::internal::load_keyword_names(keyword_path, error);
    }

    cpptr::dna_result write_output(const std::filesystem::path& source_path, const std::filesystem::path& dna_directory, const std::vector<dna_event>& events) override {
        return cpptr::internal::write_output(source_path, dna_directory, events);
    }
};

class default_config_loader_port : public config_loader_port {
public:
    configuration_load_result load(const std::filesystem::path& config_path) override {
        return cpptr::internal::configuration_loader::load(config_path);
    }
};

} // namespace

dna_pipeline_service::dna_pipeline_service(
    std::unique_ptr<config_loader_port> config_loader,
    std::unique_ptr<file_system_port> file_system)
    : config_loader_(std::move(config_loader)),
      file_system_(std::move(file_system)) {}

cpptr::dna_result dna_pipeline_service::generate(const cpptr::dna_request& request) {
    std::optional<loaded_configuration> pending_configuration;
    if (!cached_configuration_ || cached_config_path_ != request.config_path) {
        auto loaded = config_loader_->load(request.config_path);
        if (!loaded.ok()) return cpptr::dna_result::failure(*loaded.error);
        pending_configuration = std::move(*loaded.configuration);
    }

    cpptr::dna_error io_err;
    const auto src = file_system_->read_source_lines(request.source_path, io_err);
    if (!src) return cpptr::dna_result::failure(std::move(io_err));

    if (pending_configuration) {
        cpptr::dna_error keyword_error;
        const auto keyword_names = file_system_->load_keyword_names(
            pending_configuration->keyword_path, keyword_error);
        if (!keyword_names) return cpptr::dna_result::failure(std::move(keyword_error));

        cached_config_path_ = request.config_path;
        cached_configuration_ = std::move(*pending_configuration);
        cached_keyword_names_ = *keyword_names;
    }

    const auto scanned = scan_source(*src, *cached_keyword_names_, request.source_path);
    return scanned.error ? cpptr::dna_result::failure(*scanned.error) : file_system_->write_output(request.source_path, request.dna_directory, scanned.events);
}

std::unique_ptr<dna_generation_service> make_dna_generation_service() {
    return std::make_unique<dna_pipeline_service>(
        std::make_unique<default_config_loader_port>(),
        std::make_unique<default_file_system_port>()
    );
}

}

namespace cpptr {

std::unique_ptr<dna_generation_service> make_dna_generation_service() {
    return internal::make_dna_generation_service();
}

}
