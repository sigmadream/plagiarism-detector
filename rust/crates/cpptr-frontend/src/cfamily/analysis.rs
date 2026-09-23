//! Structural analysis of a C/C++ token stream: bracket matching, function definitions,
//! prototypes and the root functions from which static call tracing starts.

use std::collections::{HashMap, HashSet};

use super::lexer::{Kind, Token};

#[derive(Debug, Clone)]
pub struct FunctionDef {
    pub name: String,
    /// First token of the declaration (return type).
    pub span_begin: usize,
    pub params_open: usize,
    pub params_close: usize,
    pub body_open: usize,
    pub body_close: usize,
    /// Emitted in place instead of only at call sites.
    pub root: bool,
}

#[derive(Debug, Clone, Copy)]
pub struct Prototype {
    /// Index of the terminating `;`.
    pub end: usize,
}

#[derive(Debug, Default)]
pub struct Analysis {
    pub tokens: Vec<Token>,
    /// Matching bracket index for `(`, `)`, `{` and `}`.
    pub matching: Vec<Option<usize>>,
    /// Innermost enclosing `{`.
    enclosing_brace: Vec<Option<usize>>,
    /// True when the `{` opens a class, struct or union body.
    class_brace: Vec<bool>,
    pub functions: Vec<FunctionDef>,
    prototypes: Vec<Prototype>,
    function_by_span_begin: HashMap<usize, usize>,
    prototype_by_span_begin: HashMap<usize, usize>,
    declared_name_indices: HashSet<usize>,
    /// First definition of a name wins.
    function_by_name: HashMap<String, usize>,
}

impl Analysis {
    pub fn new(tokens: Vec<Token>) -> Self {
        let mut analysis = Analysis {
            tokens,
            ..Default::default()
        };
        analysis.match_brackets();
        analysis.collect_declarations();
        analysis.mark_roots();
        analysis
    }

    fn text(&self, index: usize) -> &str {
        &self.tokens[index].text
    }

    pub fn function_at(&self, index: usize) -> Option<usize> {
        self.function_by_span_begin.get(&index).copied()
    }

    pub fn prototype_at(&self, index: usize) -> Option<Prototype> {
        self.prototype_by_span_begin
            .get(&index)
            .map(|&p| self.prototypes[p])
    }

    pub fn find_function(&self, name: &str) -> Option<usize> {
        self.function_by_name.get(name).copied()
    }

    /// Identifier immediately followed by `(` that is neither a definition nor a prototype name.
    pub fn is_call_site(&self, index: usize) -> bool {
        self.tokens[index].kind == Kind::Identifier
            && index + 1 < self.tokens.len()
            && self.text(index + 1) == "("
            && !self.declared_name_indices.contains(&index)
    }

    fn match_brackets(&mut self) {
        let n = self.tokens.len();
        self.matching = vec![None; n];
        self.enclosing_brace = vec![None; n];
        self.class_brace = vec![false; n];
        let (mut parens, mut braces): (Vec<usize>, Vec<usize>) = (Vec::new(), Vec::new());
        for i in 0..n {
            self.enclosing_brace[i] = braces.last().copied();
            let (stack, is_close) = match self.tokens[i].text.as_str() {
                "(" => (&mut parens, false),
                ")" => (&mut parens, true),
                "{" => (&mut braces, false),
                "}" => (&mut braces, true),
                _ => continue,
            };
            if !is_close {
                stack.push(i);
            } else if let Some(open) = stack.pop() {
                self.matching[i] = Some(open);
                self.matching[open] = Some(i);
            }
        }

        // A `{` opens a class body when walking back over the class head reaches class/struct/union.
        for i in 0..n {
            if self.text(i) != "{" {
                continue;
            }
            for k in (0..i).rev() {
                let t = &self.tokens[k];
                if matches!(t.text.as_str(), "class" | "struct" | "union") {
                    self.class_brace[i] = true;
                    break;
                }
                let head_token = matches!(t.kind, Kind::Identifier | Kind::Keyword)
                    || matches!(t.text.as_str(), ":" | "," | "::" | "<" | ">");
                if !head_token {
                    break;
                }
            }
        }
    }

    fn in_class_scope(&self, index: usize) -> bool {
        self.enclosing_brace[index].is_some_and(|brace| self.class_brace[brace])
    }

    /// Walks back from the declared name over the tokens that can form its return type.
    /// Non-type keywords such as `return` or `else` end the walk.
    fn declaration_span_begin(&self, name_index: usize) -> usize {
        let mut k = name_index;
        while k > 0 {
            let p = &self.tokens[k - 1];
            let type_token = p.kind == Kind::Identifier
                || (p.kind == Kind::Keyword && is_type_keyword(&p.text))
                || matches!(p.text.as_str(), "*" | "&" | "&&" | "::" | "<" | ">");
            if !type_token {
                break;
            }
            k -= 1;
        }
        k
    }

    /// A declaration starts at the beginning of the file, after a statement or block boundary,
    /// or after an access specifier. `return n * f(x);` and `case 1: f(x);` are calls.
    fn can_start_declaration(&self, span_begin: usize) -> bool {
        if span_begin == 0 {
            return true;
        }
        match self.text(span_begin - 1) {
            ";" | "{" | "}" => true,
            ":" if span_begin >= 2 => {
                matches!(self.text(span_begin - 2), "public" | "private" | "protected")
            }
            _ => false,
        }
    }

    fn preceded_by_type(&self, name_index: usize) -> bool {
        let mut k = name_index;
        // skip `Ns::` qualifiers
        while k >= 2 && self.text(k - 1) == "::" && self.tokens[k - 2].kind == Kind::Identifier {
            k -= 2;
        }
        if k == 0 {
            return false;
        }
        let p = &self.tokens[k - 1];
        match p.kind {
            Kind::Identifier => true,
            Kind::Keyword => is_type_keyword(&p.text),
            _ => matches!(p.text.as_str(), "*" | "&" | ">"),
        }
    }

    /// Given the `)` of a parameter list, finds the `{` that opens a function body.
    fn find_body_open(&self, params_close: usize) -> Option<usize> {
        let tokens = &self.tokens;
        let mut k = params_close + 1;
        let mut trailing_return = false;
        while k < tokens.len() {
            let t = &tokens[k];
            match t.text.as_str() {
                "{" => return Some(k),
                "->" => {
                    trailing_return = true;
                    k += 1;
                    continue;
                }
                _ => {}
            }
            if trailing_return
                && (matches!(t.kind, Kind::Identifier | Kind::Keyword)
                    || matches!(t.text.as_str(), "::" | "*" | "&"))
            {
                k += 1;
                continue;
            }
            if is_trailing_qualifier(&t.text) {
                k += 1;
                continue;
            }
            if t.text == "(" {
                k = self.matching[k]? + 1;
                continue;
            }
            if t.text != ":" {
                return None;
            }

            // constructor initializer list: `: a(1), b{2} {`
            k += 1;
            let mut paren_depth = 0usize;
            while k < tokens.len() {
                match tokens[k].text.as_str() {
                    "(" => paren_depth += 1,
                    ")" => {
                        if paren_depth == 0 {
                            return None;
                        }
                        paren_depth -= 1;
                    }
                    ";" | "}" if paren_depth == 0 => return None,
                    "{" if paren_depth == 0 => {
                        if matches!(self.text(k - 1), ")" | "}") {
                            return Some(k);
                        }
                        k = self.matching[k]?; // skip brace initializer
                    }
                    _ => {}
                }
                k += 1;
            }
            return None;
        }
        None
    }

    /// Given the `)` of a parameter list, finds the `;` that ends a declaration, if any.
    fn declaration_end(&self, params_close: usize) -> Option<usize> {
        let tokens = &self.tokens;
        let mut k = params_close + 1;
        while k < tokens.len() {
            let t = &tokens[k];
            if t.text == ";" {
                return Some(k);
            }
            if is_trailing_qualifier(&t.text) {
                k += 1;
                continue;
            }
            if t.text == "(" {
                k = self.matching[k]? + 1;
                continue;
            }
            if t.text == "=" {
                // `= 0;`, `= default;`, `= delete;`
                for (offset, token) in tokens[k + 1..].iter().enumerate() {
                    match token.text.as_str() {
                        ";" => return Some(k + 1 + offset),
                        "{" | "}" | "(" => return None,
                        _ => {}
                    }
                }
                return None;
            }
            return None;
        }
        None
    }

    fn collect_declarations(&mut self) {
        for i in 0..self.tokens.len().saturating_sub(1) {
            if self.tokens[i].kind != Kind::Identifier || self.text(i + 1) != "(" {
                continue;
            }
            let params_open = i + 1;
            let Some(params_close) = self.matching[params_open] else {
                continue;
            };

            if let Some(body_open) = self.find_body_open(params_close) {
                let Some(body_close) = self.matching[body_open] else {
                    continue;
                };
                let def = FunctionDef {
                    name: self.tokens[i].text.clone(),
                    span_begin: self.declaration_span_begin(i),
                    params_open,
                    params_close,
                    body_open,
                    body_close,
                    root: false,
                };
                let position = self.functions.len();
                self.function_by_span_begin.insert(def.span_begin, position);
                self.function_by_name.entry(def.name.clone()).or_insert(position);
                self.declared_name_indices.insert(i);
                self.functions.push(def);
                continue;
            }

            if !self.preceded_by_type(i) && !self.in_class_scope(i) {
                continue;
            }
            let span_begin = self.declaration_span_begin(i);
            if !self.can_start_declaration(span_begin) {
                continue;
            }
            if let Some(end) = self.declaration_end(params_close) {
                self.prototype_by_span_begin
                    .insert(span_begin, self.prototypes.len());
                self.declared_name_indices.insert(i);
                self.prototypes.push(Prototype { end });
            }
        }
    }

    /// A function is a root when nothing in the file calls it, or when it is unreachable from
    /// the other roots (for example a recursive island). Roots are emitted in place; every other
    /// function only appears inlined at its call sites.
    fn mark_roots(&mut self) {
        let called: HashSet<&str> = (0..self.tokens.len())
            .filter(|&i| self.is_call_site(i))
            .map(|i| self.text(i))
            .collect();

        let edges: Vec<Vec<usize>> = self
            .functions
            .iter()
            .map(|def| {
                (def.params_open..=def.body_close)
                    .filter(|&i| self.is_call_site(i))
                    .filter_map(|i| self.find_function(self.text(i)))
                    .collect()
            })
            .collect();

        let count = self.functions.len();
        let mut roots = vec![false; count];
        let mut reachable = vec![false; count];
        let visit = |start: usize, reachable: &mut Vec<bool>| {
            let mut stack = vec![start];
            while let Some(f) = stack.pop() {
                if reachable[f] {
                    continue;
                }
                reachable[f] = true;
                stack.extend(edges[f].iter().copied().filter(|&g| !reachable[g]));
            }
        };

        for (f, def) in self.functions.iter().enumerate() {
            if !called.contains(def.name.as_str()) {
                roots[f] = true;
                visit(f, &mut reachable);
            }
        }
        for f in 0..count {
            if !reachable[f] {
                roots[f] = true;
                visit(f, &mut reachable);
            }
        }
        drop(called);
        for (def, root) in self.functions.iter_mut().zip(roots) {
            def.root = root;
        }
    }
}

/// Keywords that can precede a declared name. Used to tell `int f(int);` from `f(x);`.
fn is_type_keyword(text: &str) -> bool {
    matches!(
        text,
        "void"
            | "int"
            | "char"
            | "bool"
            | "short"
            | "float"
            | "double"
            | "signed"
            | "long"
            | "unsigned"
            | "const"
            | "volatile"
            | "static"
            | "inline"
            | "struct"
            | "class"
            | "union"
            | "enum"
            | "virtual"
            | "friend"
            | "typedef"
            | "extern"
            | "auto"
            | "constexpr"
            | "explicit"
            | "wchar_t"
            | "char8_t"
            | "char16_t"
            | "char32_t"
            | "register"
            | "mutable"
            | "typename"
    )
}

/// Qualifiers that may follow a parameter list before the body or the semicolon.
fn is_trailing_qualifier(text: &str) -> bool {
    matches!(
        text,
        "const"
            | "volatile"
            | "noexcept"
            | "override"
            | "final"
            | "throw"
            | "constexpr"
            | "mutable"
            | "&"
            | "&&"
    )
}
