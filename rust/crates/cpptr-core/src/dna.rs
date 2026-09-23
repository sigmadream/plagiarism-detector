//! Program DNA files: one token per line, optionally followed by the source line and column.
//!
//! ```text
//! INT     2   1
//! FOR     6   1
//! ```
//!
//! Lines that are empty or start with `#` or `%` are ignored, as in the C++ loader.

use std::collections::HashMap;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use rayon::prelude::*;

/// Dense token identifier assigned by [`TokenInterner`].
pub type TokenId = u32;

/// Maps DNA token names to dense identifiers shared by every sequence in a comparison.
#[derive(Debug, Default, Clone)]
pub struct TokenInterner {
    ids: HashMap<String, TokenId>,
    names: Vec<String>,
}

impl TokenInterner {
    pub fn intern(&mut self, name: &str) -> TokenId {
        if let Some(&id) = self.ids.get(name) {
            return id;
        }
        let id = TokenId::try_from(self.names.len()).expect("more than u32::MAX distinct tokens");
        self.ids.insert(name.to_owned(), id);
        self.names.push(name.to_owned());
        id
    }

    pub fn len(&self) -> usize {
        self.names.len()
    }

    pub fn is_empty(&self) -> bool {
        self.names.is_empty()
    }

    pub fn name(&self, id: TokenId) -> &str {
        &self.names[id as usize]
    }
}

/// A DNA sequence before interning: token names and their source lines (0 when unknown).
#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct RawDna {
    pub tokens: Vec<String>,
    pub lines: Vec<u32>,
}

/// A DNA sequence with interned tokens.
#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct Dna {
    pub tokens: Vec<TokenId>,
    pub lines: Vec<u32>,
}

impl RawDna {
    pub fn parse(text: &str) -> Self {
        let mut dna = RawDna::default();
        for line in text.lines() {
            if line.is_empty() || line.starts_with('#') || line.starts_with('%') {
                continue;
            }
            let mut fields = line.split_ascii_whitespace();
            let Some(token) = fields.next() else { continue };
            // Like `istream >> int`, a missing or malformed line number reads as 0.
            let source_line = fields
                .next()
                .and_then(|field| field.parse::<i64>().ok())
                .filter(|&value| value > 0)
                .map_or(0, |value| u32::try_from(value).unwrap_or(u32::MAX));
            dna.tokens.push(token.to_owned());
            dna.lines.push(source_line);
        }
        dna
    }

    pub fn read(path: &Path) -> io::Result<Self> {
        let bytes = fs::read(path)?;
        Ok(Self::parse(&String::from_utf8_lossy(&bytes)))
    }

    pub fn intern(&self, interner: &mut TokenInterner) -> Dna {
        Dna {
            tokens: self.tokens.iter().map(|token| interner.intern(token)).collect(),
            lines: self.lines.clone(),
        }
    }
}

/// Every `.DNA` file of a directory, sorted by file name, interned with one shared vocabulary.
#[derive(Debug, Default)]
pub struct DnaCorpus {
    pub interner: TokenInterner,
    names: Vec<String>,
    sequences: Vec<Dna>,
    index: HashMap<String, usize>,
}

impl DnaCorpus {
    /// Loads all regular files with the exact extension `.DNA`, reading them in parallel.
    pub fn load_directory(directory: &Path) -> io::Result<Self> {
        let mut paths: Vec<PathBuf> = Vec::new();
        for entry in fs::read_dir(directory)? {
            let entry = entry?;
            let path = entry.path();
            if entry.file_type()?.is_file() && path.extension().is_some_and(|ext| ext == "DNA") {
                paths.push(path);
            }
        }
        paths.sort();

        let raw: Vec<RawDna> = paths
            .par_iter()
            .map(|path| RawDna::read(path))
            .collect::<io::Result<_>>()?;

        let mut corpus = DnaCorpus::default();
        for (path, raw) in paths.iter().zip(&raw) {
            let name = file_name(path);
            let dna = raw.intern(&mut corpus.interner);
            corpus.insert(name, dna);
        }
        Ok(corpus)
    }

    /// Builds a corpus from in-memory sequences, keeping their order.
    pub fn from_raw(sequences: impl IntoIterator<Item = (String, RawDna)>) -> Self {
        let mut corpus = DnaCorpus::default();
        for (name, raw) in sequences {
            let dna = raw.intern(&mut corpus.interner);
            corpus.insert(name, dna);
        }
        corpus
    }

    fn insert(&mut self, name: String, dna: Dna) -> usize {
        let position = self.sequences.len();
        self.index.insert(name.clone(), position);
        self.names.push(name);
        self.sequences.push(dna);
        position
    }

    /// Returns the position of `name`, loading it from `directory` when it was not part of the
    /// `.DNA` scan (for example a manifest entry with another extension).
    pub fn resolve(&mut self, directory: &Path, name: &str) -> io::Result<usize> {
        if let Some(&position) = self.index.get(name) {
            return Ok(position);
        }
        let raw = RawDna::read(&directory.join(name))?;
        let dna = raw.intern(&mut self.interner);
        Ok(self.insert(name.to_owned(), dna))
    }

    pub fn len(&self) -> usize {
        self.sequences.len()
    }

    pub fn is_empty(&self) -> bool {
        self.sequences.is_empty()
    }

    pub fn name(&self, position: usize) -> &str {
        &self.names[position]
    }

    pub fn dna(&self, position: usize) -> &Dna {
        &self.sequences[position]
    }

    /// Token frequencies in percent over the files found by [`Self::load_directory`].
    /// Tokens seen only in files added later through [`Self::resolve`] have no frequency.
    pub fn frequencies(&self, scanned: usize) -> Vec<Option<f64>> {
        let mut counts = vec![0u64; self.interner.len()];
        let mut total = 0u64;
        for dna in &self.sequences[..scanned] {
            for &token in &dna.tokens {
                counts[token as usize] += 1;
                total += 1;
            }
        }
        counts
            .into_iter()
            .map(|count| (total > 0 && count > 0).then(|| (count as f64 / total as f64) * 100.0))
            .collect()
    }
}

pub fn file_name(path: &Path) -> String {
    path.file_name()
        .map(|name| name.to_string_lossy().into_owned())
        .unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_tokens_and_lines_like_the_cpp_loader() {
        let dna = RawDna::parse("INT\t0\t0\r\n# comment\n%meta\n\nFOR 6 1\n  \nRETURN\nX abc\n");
        assert_eq!(dna.tokens, ["INT", "FOR", "RETURN", "X"]);
        assert_eq!(dna.lines, [0, 6, 0, 0]);
    }

    #[test]
    fn interner_reuses_ids() {
        let mut interner = TokenInterner::default();
        let a = interner.intern("A");
        let b = interner.intern("B");
        assert_eq!(interner.intern("A"), a);
        assert_ne!(a, b);
        assert_eq!(interner.name(b), "B");
    }
}
