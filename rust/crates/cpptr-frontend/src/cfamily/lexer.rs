//! Line-oriented C/C++ lexer. Works on bytes: identifiers are ASCII, and every other byte that is
//! not part of a known operator becomes a one-byte punctuation token, as in the C++ scanner.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Identifier,
    Keyword,
    Number,
    Punct,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Token {
    pub kind: Kind,
    /// Source spelling (lossy for non-ASCII bytes, which never take part in comparisons).
    pub text: String,
    /// DNA token name; empty when the token has no DNA representation.
    pub dna: &'static str,
    /// 1-based source line.
    pub line: u32,
    /// 0-based byte column.
    pub column: u32,
}

/// Keywords that map to a DNA token name in the keyword table.
fn keyword_dna(text: &str) -> Option<&'static str> {
    Some(match text {
        "void" => "VOID",
        "int" => "INT",
        "char" => "CHAR",
        "bool" => "BOOL",
        "short" => "SHORT",
        "float" => "FLOAT",
        "double" => "DOUBLE",
        "signed" => "SIGNED",
        "long" => "LONG",
        "unsigned" => "UNSIGNED",
        "do" => "DO",
        "true" => "TRUE",
        "false" => "FALSE",
        "struct" => "STRUCT",
        "static" => "STATIC",
        "continue" => "CONTINUE",
        "switch" => "SWITCH",
        "case" => "CASE",
        "default" => "DEFAULT",
        "break" => "BREAK",
        "return" => "RETURN",
        "goto" => "GOTO",
        "NULL" | "nullptr" => "NULL",
        "typedef" => "TYPEDEF",
        "sizeof" => "SIZEOF",
        "const" => "CONST",
        "volatile" => "VOLATILE",
        "inline" => "INLINE",
        "public" => "PUBLIC",
        "private" => "PRIVATE",
        "friend" => "FRIEND",
        "protected" => "PROTECTED",
        "new" => "NEW",
        "delete" => "DELETE",
        "virtual" => "VIRTUAL",
        "try" => "TRY",
        "catch" => "CATCH",
        "throw" => "THROW",
        "class" => "CLASS",
        "namespace" => "NAMESPACE",
        "if" => "IF",
        "else" => "ELSE",
        "for" => "FOR",
        "while" => "WHILE",
        _ => return None,
    })
}

/// Reserved words without a DNA token. They are never treated as function names.
fn is_reserved_word(text: &str) -> bool {
    matches!(
        text,
        "auto"
            | "register"
            | "extern"
            | "enum"
            | "union"
            | "template"
            | "typename"
            | "using"
            | "operator"
            | "this"
            | "static_cast"
            | "dynamic_cast"
            | "reinterpret_cast"
            | "const_cast"
            | "decltype"
            | "alignof"
            | "alignas"
            | "noexcept"
            | "constexpr"
            | "consteval"
            | "constinit"
            | "explicit"
            | "mutable"
            | "static_assert"
            | "thread_local"
            | "export"
            | "import"
            | "module"
            | "co_await"
            | "co_return"
            | "co_yield"
            | "concept"
            | "requires"
            | "wchar_t"
            | "char8_t"
            | "char16_t"
            | "char32_t"
            | "asm"
            | "typeid"
            | "and"
            | "or"
            | "not"
            | "xor"
            | "bitand"
            | "bitor"
            | "compl"
            | "and_eq"
            | "or_eq"
            | "xor_eq"
            | "not_eq"
            | "override"
            | "final"
    )
}

/// Longest spellings first so that `<<=` wins over `<<` and `<`.
const OPERATORS: &[(&str, &str)] = &[
    ("<<=", "SHL_EQ"),
    (">>=", "SHR_EQ"),
    ("->*", ""),
    ("...", ""),
    ("<<", "SHL"),
    (">>", "SHR"),
    ("<=", "LT_EQ"),
    (">=", "GT_EQ"),
    ("==", "EQ"),
    ("!=", "NOT_EQ"),
    ("&&", "AND"),
    ("||", "OR"),
    ("++", "INCR"),
    ("--", "DECR"),
    ("+=", "PLUS_EQ"),
    ("-=", "MINUS_EQ"),
    ("*=", "TIMES_EQ"),
    ("/=", "DIV_EQ"),
    ("%=", "MOD_EQ"),
    ("&=", "BITAND_EQ"),
    ("^=", "BITXOR_EQ"),
    ("|=", "BITOR_EQ"),
    ("->", ""),
    ("::", ""),
    (".*", ""),
    ("<", "LT"),
    (">", "GT"),
    ("+", "PLUS"),
    ("-", "MINUS"),
    ("*", "MULTIPLE"),
    ("/", "DIVIDE"),
    ("%", "MOD"),
    ("&", "BIT_AND"),
    ("|", "BIT_OR"),
    ("^", "BIT_XOR"),
    ("!", "NOT"),
    ("=", "ASSIGN_EQ"),
    ("{", "BLOCK_START"),
    ("}", "BLOCK_END"),
];

/// `isspace` in the C locale.
pub fn is_space(byte: u8) -> bool {
    matches!(byte, b' ' | b'\t' | b'\n' | b'\x0b' | b'\x0c' | b'\r')
}

fn is_identifier_start(byte: u8) -> bool {
    byte.is_ascii_alphabetic() || byte == b'_'
}

fn is_identifier_char(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || byte == b'_'
}

fn text(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).into_owned()
}

/// Blanks out string and character literals and comments, keeping column positions.
/// `in_block_comment` carries an open `/* ... */` across lines.
pub fn strip_literals_and_comments(line: &[u8], in_block_comment: &mut bool) -> Vec<u8> {
    let mut masked = line.to_vec();
    let (mut in_string, mut in_char, mut escaped) = (false, false, false);
    let mut index = 0;
    while index < masked.len() {
        let ch = masked[index];
        let next = masked.get(index + 1).copied().unwrap_or(0);
        if *in_block_comment {
            if ch == b'*' && next == b'/' {
                *in_block_comment = false;
                masked[index + 1] = b' ';
                masked[index] = b' ';
                index += 2;
                continue;
            }
            masked[index] = b' ';
            index += 1;
            continue;
        }
        if in_string || in_char {
            let quote = if in_string { b'"' } else { b'\'' };
            if !escaped && ch == quote {
                in_string = false;
                in_char = false;
            }
            escaped = !escaped && ch == b'\\';
            masked[index] = b' ';
            index += 1;
            continue;
        }
        escaped = false;
        if ch == b'/' && next == b'/' {
            masked[index..].fill(b' ');
            break;
        }
        if ch == b'/' && next == b'*' {
            *in_block_comment = true;
            masked[index] = b' ';
            masked[index + 1] = b' ';
            index += 2;
            continue;
        }
        if ch == b'"' || ch == b'\'' {
            if ch == b'"' {
                in_string = true;
            } else {
                in_char = true;
            }
            masked[index] = b' ';
        }
        index += 1;
    }
    masked
}

pub fn lex_line(line: &[u8], line_number: u32, tokens: &mut Vec<Token>) {
    let mut i = 0;
    while i < line.len() {
        let ch = line[i];
        let column = i as u32;
        if is_space(ch) {
            i += 1;
            continue;
        }

        if is_identifier_start(ch) {
            let mut j = i;
            while j < line.len() && is_identifier_char(line[j]) {
                j += 1;
            }
            let word = text(&line[i..j]);
            let (kind, dna) = match keyword_dna(&word) {
                Some(dna) => (Kind::Keyword, dna),
                None if is_reserved_word(&word) => (Kind::Keyword, ""),
                None => (Kind::Identifier, ""),
            };
            tokens.push(Token {
                kind,
                text: word,
                dna,
                line: line_number,
                column,
            });
            i = j;
            continue;
        }

        let leading_dot = ch == b'.' && line.get(i + 1).is_some_and(u8::is_ascii_digit);
        if ch.is_ascii_digit() || leading_dot {
            let mut j = i + 1;
            while j < line.len() {
                let c = line[j];
                if is_identifier_char(c) || c == b'.' {
                    j += 1;
                    continue;
                }
                if (c == b'+' || c == b'-') && matches!(line[j - 1], b'e' | b'E') {
                    j += 1;
                    continue;
                }
                break;
            }
            tokens.push(Token {
                kind: Kind::Number,
                text: text(&line[i..j]),
                dna: "",
                line: line_number,
                column,
            });
            i = j;
            continue;
        }

        let rest = &line[i..];
        match OPERATORS
            .iter()
            .find(|(spelling, _)| rest.starts_with(spelling.as_bytes()))
        {
            Some(&(spelling, dna)) => {
                tokens.push(Token {
                    kind: Kind::Punct,
                    text: spelling.to_owned(),
                    dna,
                    line: line_number,
                    column,
                });
                i += spelling.len();
            }
            None => {
                tokens.push(Token {
                    kind: Kind::Punct,
                    text: text(&line[i..=i]),
                    dna: "",
                    line: line_number,
                    column,
                });
                i += 1;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn lex(source: &str) -> Vec<(Kind, String, &'static str)> {
        let mut tokens = Vec::new();
        lex_line(source.as_bytes(), 1, &mut tokens);
        tokens.into_iter().map(|t| (t.kind, t.text, t.dna)).collect()
    }

    #[test]
    fn strips_literals_and_comments_keeping_columns() {
        let mut block = false;
        let masked = strip_literals_and_comments(br#"a = "x\"y"; /* c"#, &mut block);
        assert_eq!(masked, b"a =       ;     ");
        assert!(block);
        let masked = strip_literals_and_comments(b"still */ b = 'c'; // tail", &mut block);
        assert_eq!(masked, b"         b =    ;        ");
        assert!(!block);
    }

    #[test]
    fn lexes_operators_longest_first() {
        let tokens = lex("x <<= 1.5e+3; p->q");
        let dna: Vec<&str> = tokens.iter().map(|t| t.2).collect();
        assert_eq!(dna, ["", "SHL_EQ", "", "", "", "", ""]);
        assert_eq!(tokens[2], (Kind::Number, "1.5e+3".into(), ""));
        assert_eq!(tokens[5].1, "->");
    }

    #[test]
    fn classifies_keywords() {
        let tokens = lex("nullptr template foo");
        assert_eq!(tokens[0], (Kind::Keyword, "nullptr".into(), "NULL"));
        assert_eq!(tokens[1], (Kind::Keyword, "template".into(), ""));
        assert_eq!(tokens[2], (Kind::Identifier, "foo".into(), ""));
    }
}
