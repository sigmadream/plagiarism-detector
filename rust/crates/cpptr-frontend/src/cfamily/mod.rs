//! C and C++ front end: a port of the C++ cppTR scanner (`src/lib/dna_pipeline.cpp`).
//!
//! The source is split into lines, preprocessor lines are dropped, literals and comments are
//! masked, and the remaining tokens are mapped to DNA. Calls to functions defined in the same
//! file are traced statically: the callee body is inlined at the call site, ending with
//! `FUNC_END`.

mod analysis;
mod lexer;

use std::collections::HashMap;

use analysis::Analysis;
use lexer::{is_space, lex_line, strip_literals_and_comments, Token};

use crate::{DnaEvent, Frontend, KeywordTable};

/// Inlining limits that keep pathological call graphs from exploding the DNA length.
const MAX_INLINE_DEPTH: usize = 8;
const MAX_DNA_EVENTS: usize = 250_000;

/// Front end for C and C++ sources.
#[derive(Debug, Clone)]
pub struct CFamily {
    keywords: KeywordTable,
}

impl CFamily {
    /// `keywords` limits the emitted DNA tokens to the names in the table.
    pub fn new(keywords: KeywordTable) -> Self {
        CFamily { keywords }
    }
}

impl Frontend for CFamily {
    fn scan(&self, source: &[u8]) -> Vec<DnaEvent> {
        let lines = split_lines(source);
        let is_directive = |line: &[u8]| trim(line).first() == Some(&b'#');
        // Leading blank and preprocessor lines are skipped before comment tracking starts.
        let base = lines
            .iter()
            .position(|line| !trim(line).is_empty() && !is_directive(line))
            .unwrap_or(0);

        let mut tokens = Vec::new();
        let mut in_comment = false;
        for (index, line) in lines.iter().enumerate().skip(base) {
            let masked = strip_literals_and_comments(line, &mut in_comment);
            if is_directive(line) {
                continue;
            }
            lex_line(&masked, (index + 1) as u32, &mut tokens);
        }

        let analysis = Analysis::new(tokens);
        let mut emitter = Emitter {
            analysis: &analysis,
            keywords: &self.keywords,
            events: Vec::new(),
            call_stack: Vec::new(),
        };
        emitter.emit_range(0, analysis.tokens.len(), 0, None);
        emitter.events
    }
}

/// Splits on LF, CRLF and bare CR so that classic Mac line endings are not collapsed into one
/// preprocessor line. A trailing empty line is not produced.
fn split_lines(source: &[u8]) -> Vec<&[u8]> {
    let mut lines = Vec::new();
    let mut start = 0;
    let mut i = 0;
    while i < source.len() {
        match source[i] {
            b'\n' => {
                lines.push(&source[start..i]);
                start = i + 1;
            }
            b'\r' => {
                lines.push(&source[start..i]);
                if source.get(i + 1) == Some(&b'\n') {
                    i += 1;
                }
                start = i + 1;
            }
            _ => {}
        }
        i += 1;
    }
    if start < source.len() {
        lines.push(&source[start..]);
    }
    lines
}

fn trim(line: &[u8]) -> &[u8] {
    let begin = line.iter().position(|&b| !is_space(b)).unwrap_or(line.len());
    let end = line
        .iter()
        .rposition(|&b| !is_space(b))
        .map_or(begin, |last| last + 1);
    &line[begin..end]
}

/// DNA emission with static tracing of user-defined function calls.
struct Emitter<'a> {
    analysis: &'a Analysis,
    keywords: &'a KeywordTable,
    events: Vec<DnaEvent>,
    call_stack: Vec<&'a str>,
}

impl<'a> Emitter<'a> {
    fn emit(&mut self, dna: &'static str, at: &Token) {
        if dna.is_empty() || !self.keywords.contains(dna) {
            return;
        }
        self.events.push(DnaEvent {
            token: dna,
            line: at.line,
            column: at.column,
        });
    }

    fn emit_function(&mut self, function: usize, depth: usize) {
        let f = &self.analysis.functions[function];
        self.emit_range(f.span_begin, f.params_open, depth, Some(function));
        self.emit_range(f.params_open, f.params_close + 1, depth, Some(function));
        self.emit_range(f.body_open, f.body_close + 1, depth, Some(function));
    }

    fn inline_callee(&mut self, function: usize, depth: usize, at: &Token) {
        let name = self.analysis.functions[function].name.as_str();
        let on_stack = self.call_stack.contains(&name);
        if depth < MAX_INLINE_DEPTH && self.events.len() < MAX_DNA_EVENTS && !on_stack {
            self.call_stack.push(name);
            self.emit_function(function, depth + 1);
            self.call_stack.pop();
        }
        self.emit("FUNC_END", at);
    }

    fn emit_range(&mut self, begin: usize, end: usize, depth: usize, current: Option<usize>) {
        let analysis = self.analysis;
        let tokens = &analysis.tokens;
        let mut pending: HashMap<usize, usize> = HashMap::new(); // call `)` index -> callee
        let mut i = begin;
        while i < end {
            if let Some(def) = analysis.function_at(i).filter(|&def| Some(def) != current) {
                if analysis.functions[def].root {
                    self.emit_function(def, depth);
                }
                i = analysis.functions[def].body_close + 1;
                continue;
            }
            if let Some(prototype) = analysis.prototype_at(i) {
                i = prototype.end + 1;
                continue;
            }

            let token = &tokens[i];
            if analysis.is_call_site(i) {
                match analysis.find_function(&token.text) {
                    Some(callee) => {
                        self.emit("FUNC_CALL", token);
                        match analysis.matching[i + 1] {
                            Some(close) if close < end => {
                                pending.insert(close, callee);
                            }
                            _ => self.inline_callee(callee, depth, token),
                        }
                    }
                    None => self.emit("UNTRACKABLE_FUNC_CALL", token),
                }
                i += 1;
                continue;
            }

            if token.text == ")" {
                if let Some(callee) = pending.remove(&i) {
                    self.inline_callee(callee, depth, token);
                }
                i += 1;
                continue;
            }

            self.emit(token.dna, token);
            i += 1;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scan(source: &str) -> Vec<&'static str> {
        CFamily::new(KeywordTable::default_cpp())
            .scan(source.as_bytes())
            .into_iter()
            .map(|e| e.token)
            .collect()
    }

    #[test]
    fn splits_all_line_endings() {
        assert_eq!(split_lines(b"a\r\nb\rc\n\nd"), [&b"a"[..], b"b", b"c", b"", b"d"]);
        assert_eq!(split_lines(b"a\n"), [&b"a"[..]]);
    }

    #[test]
    fn inlines_user_functions_at_call_sites() {
        let dna = scan("#include <cstdio>\nint sq(int x) { return x * x; }\nint main() { return sq(2); }\n");
        assert_eq!(
            dna,
            [
                "INT",
                "BLOCK_START",
                "RETURN",
                "FUNC_CALL", // main, call to sq
                "INT",
                "INT",
                "BLOCK_START",
                "RETURN",
                "MULTIPLE",
                "BLOCK_END", // inlined sq
                "FUNC_END",
                "BLOCK_END",
            ]
        );
    }

    #[test]
    fn library_calls_are_untrackable_and_prototypes_are_skipped() {
        let dna = scan("int f(int);\nint main() { printf(\"%d\", 1); }\n");
        assert_eq!(dna, ["INT", "BLOCK_START", "UNTRACKABLE_FUNC_CALL", "BLOCK_END"]);
    }

    #[test]
    fn recursion_is_inlined_once() {
        // A root is emitted without being on the call stack, so its first self call is inlined
        // and the nested one stops at FUNC_END.
        let dna = scan("int f(int n) { return f(n - 1); }\n");
        let body = ["INT", "INT", "BLOCK_START", "RETURN", "FUNC_CALL", "MINUS"];
        let mut expected = body.to_vec();
        expected.extend(body);
        expected.extend(["FUNC_END", "BLOCK_END", "FUNC_END", "BLOCK_END"]);
        assert_eq!(dna, expected);
    }
}
