//! Front end configuration: language selection and the keyword table that limits DNA tokens.
//!
//! The default configuration and keyword tables are compiled into the binary, so a release
//! archive needs no resource directory. An INI file in the C++ `cconfig.ini` format can still
//! override them.

use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Arc;

use crate::{CFamily, Frontend, GenerateError};

const CPP_KEYWORDS: &str = include_str!("../resources/cpp_keyword.tbl");
const C_KEYWORDS: &str = include_str!("../resources/c_keyword.tbl");

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Language {
    C,
    Cpp,
}

impl Language {
    /// Parses a language name such as `C` or `cpp`.
    pub fn parse(value: &str) -> Option<Self> {
        match value.to_ascii_uppercase().as_str() {
            "C" => Some(Language::C),
            "CPP" | "C++" => Some(Language::Cpp),
            _ => None,
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Language::C => "C",
            Language::Cpp => "CPP",
        }
    }

    /// Source file extensions handled by this language's front end.
    pub fn extensions(self) -> &'static [&'static str] {
        match self {
            Language::C => &["c", "h"],
            Language::Cpp => &["cpp", "cc", "cxx", "hpp", "hh", "hxx"],
        }
    }

    /// Language for a source path by extension (case-insensitive).
    pub fn from_path(path: &Path) -> Option<Self> {
        let extension = path.extension()?.to_str()?.to_ascii_lowercase();
        [Language::C, Language::Cpp]
            .into_iter()
            .find(|language| language.extensions().contains(&extension.as_str()))
    }
}

/// Token names allowed in DNA output. Column 1 of a `.tbl` file; the other columns are
/// historical metadata and are ignored.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct KeywordTable(Arc<HashSet<String>>);

impl KeywordTable {
    pub fn parse(text: &str) -> Self {
        KeywordTable(Arc::new(
            text.lines()
                .filter_map(|line| line.split_ascii_whitespace().next())
                .map(str::to_owned)
                .collect(),
        ))
    }

    pub fn default_cpp() -> Self {
        Self::parse(CPP_KEYWORDS)
    }

    pub fn default_c() -> Self {
        Self::parse(C_KEYWORDS)
    }

    pub fn load(path: &Path) -> Result<Self, GenerateError> {
        let text =
            fs::read(path).map_err(|_| GenerateError::new("keyword table could not be opened", path))?;
        let table = Self::parse(&String::from_utf8_lossy(&text));
        if table.0.is_empty() {
            return Err(GenerateError::new(
                "keyword table did not contain any token names",
                path,
            ));
        }
        Ok(table)
    }

    pub fn contains(&self, token: &str) -> bool {
        self.0.contains(token)
    }
}

/// Loaded configuration for DNA generation.
#[derive(Debug, Clone)]
pub struct Config {
    pub language: Language,
    pub keywords: KeywordTable,
    /// The INI file this configuration came from; `None` for the built-in default.
    pub path: Option<PathBuf>,
}

impl Default for Config {
    fn default() -> Self {
        Config::builtin(Language::Cpp)
    }
}

impl Config {
    /// Built-in configuration for `language`.
    pub fn builtin(language: Language) -> Self {
        let keywords = match language {
            Language::C => KeywordTable::default_c(),
            Language::Cpp => KeywordTable::default_cpp(),
        };
        Config {
            language,
            keywords,
            path: None,
        }
    }

    /// Loads a `cconfig.ini`-style file. Keyword table paths are relative to the file.
    pub fn load(path: &Path) -> Result<Self, GenerateError> {
        if !path.exists() {
            return Err(GenerateError::new(
                "explicit config path was provided but file does not exist",
                path,
            ));
        }
        if !path.is_file() {
            return Err(GenerateError::new("config path is not a regular file", path));
        }
        let text = fs::read(path).map_err(|_| GenerateError::new("config file could not be read", path))?;
        let values = parse_ini(&String::from_utf8_lossy(&text));

        let language = match values.get("language") {
            None => Language::Cpp,
            Some(value) => Language::parse(value).ok_or_else(|| {
                GenerateError::new("unsupported language; only C and CPP are implemented", path)
            })?,
        };
        let key = match language {
            Language::C => "c_keyword_path",
            Language::Cpp => "cpp_keyword_path",
        };
        let keyword_value = values
            .get(key)
            .ok_or_else(|| GenerateError::new("keyword table key is missing from config", path))?;
        if keyword_value.is_empty() {
            return Err(GenerateError::new("keyword table path is empty", path));
        }
        let keyword_path = path.parent().unwrap_or(Path::new("")).join(keyword_value);
        if !keyword_path.is_file() {
            return Err(GenerateError::new(
                "keyword table file does not exist",
                &keyword_path,
            ));
        }

        Ok(Config {
            language,
            keywords: KeywordTable::load(&keyword_path)?,
            path: Some(path.to_path_buf()),
        })
    }

    /// Front end for this configuration's language.
    pub fn frontend(&self) -> Arc<dyn Frontend> {
        match self.language {
            Language::C | Language::Cpp => Arc::new(CFamily::new(self.keywords.clone())),
        }
    }
}

/// `key = value` pairs; section headers, comments and inline `;`/`#` comments are ignored.
fn parse_ini(text: &str) -> HashMap<String, String> {
    let mut values = HashMap::new();
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with(['#', ';']) || (line.starts_with('[') && line.ends_with(']')) {
            continue;
        }
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        let value = value.split(['#', ';']).next().unwrap_or("").trim();
        values.insert(key.trim().to_owned(), value.to_owned());
    }
    values
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builtin_tables_contain_every_emitted_token() {
        let table = KeywordTable::default_cpp();
        for token in [
            "ASSIGN_EQ",
            "WHILE",
            "BLOCK_START",
            "FUNC_CALL",
            "FUNC_END",
            "UNTRACKABLE_FUNC_CALL",
        ] {
            assert!(table.contains(token), "{token}");
        }
        assert_eq!(KeywordTable::default_c(), table);
    }

    #[test]
    fn parses_ini_like_the_cpp_loader() {
        let values = parse_ini("[LANGUAGE]\nlanguage = c ; comment\n# x=1\nkey=a#b\n");
        assert_eq!(values["language"], "c");
        assert_eq!(values["key"], "a");
        assert!(!values.contains_key("# x"));
    }

    #[test]
    fn detects_language_from_extension() {
        assert_eq!(Language::from_path(Path::new("a/b.C")), Some(Language::C));
        assert_eq!(Language::from_path(Path::new("b.cxx")), Some(Language::Cpp));
        assert_eq!(Language::from_path(Path::new("b.py")), None);
    }
}
