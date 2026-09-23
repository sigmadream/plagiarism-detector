//! Language front ends that turn source code into Program DNA.
//!
//! Every language implements [`Frontend`]; the rest of cppTR only sees the resulting
//! [`DnaEvent`] stream, so adding a language does not touch the comparison core.

pub mod cfamily;
mod config;

use std::fmt;
use std::fs;
use std::path::{Path, PathBuf};

pub use cfamily::CFamily;
pub use config::{Config, KeywordTable, Language};

/// One DNA token with the source position it came from.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DnaEvent {
    pub token: &'static str,
    /// 1-based source line.
    pub line: u32,
    /// 0-based byte column.
    pub column: u32,
}

/// A language front end. Implementations must be deterministic: the same source always yields
/// the same DNA.
pub trait Frontend: Send + Sync {
    fn scan(&self, source: &[u8]) -> Vec<DnaEvent>;
}

/// DNA generation failure with the file it concerns.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GenerateError {
    pub message: String,
    pub path: PathBuf,
}

impl GenerateError {
    fn new(message: impl Into<String>, path: &Path) -> Self {
        GenerateError {
            message: message.into(),
            path: path.to_path_buf(),
        }
    }
}

impl fmt::Display for GenerateError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for GenerateError {}

/// Renders DNA in the `.DNA` file format: `TOKEN<TAB>line<TAB>column` per line.
pub fn render_dna(events: &[DnaEvent]) -> String {
    let mut out = String::with_capacity(events.len() * 16);
    for event in events {
        out.push_str(event.token);
        out.push('\t');
        out.push_str(&event.line.to_string());
        out.push('\t');
        out.push_str(&event.column.to_string());
        out.push('\n');
    }
    out
}

/// Generates `<dna_directory>/<source file name>.DNA`. With `snapshot`, the source is also copied
/// next to it as `<name>.DNA.src`, as the C++ `generate` command does.
pub fn generate_dna(
    frontend: &dyn Frontend,
    source_path: &Path,
    dna_directory: &Path,
    snapshot: bool,
) -> Result<PathBuf, GenerateError> {
    let source = fs::read(source_path)
        .map_err(|_| GenerateError::new("source file could not be opened for DNA generation", source_path))?;
    let events = frontend.scan(&source);
    if events.is_empty() {
        return Err(GenerateError::new(
            "no DNA tokens were extracted from the source file",
            source_path,
        ));
    }

    fs::create_dir_all(dna_directory)
        .map_err(|_| GenerateError::new("failed to create DNA output directory", dna_directory))?;
    let file_name = source_path.file_name().unwrap_or_default().to_string_lossy();
    let output = dna_directory.join(format!("{file_name}.DNA"));
    fs::write(&output, render_dna(&events))
        .map_err(|_| GenerateError::new("failed while writing DNA output file", &output))?;

    if snapshot {
        let snapshot_path = dna_directory.join(format!("{file_name}.DNA.src"));
        fs::copy(source_path, &snapshot_path)
            .map_err(|_| GenerateError::new("failed to preserve source snapshot", &snapshot_path))?;
    }
    Ok(output)
}
