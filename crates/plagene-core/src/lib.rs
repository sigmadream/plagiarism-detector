//! Language-independent core of plagene: Program DNA loading, token frequencies and SC/FV local
//! alignment. Language front ends produce DNA; everything here works on DNA only.

pub mod alignment;
pub mod compare;
pub mod dna;
pub mod format;

pub use alignment::{align, Alignment, Mode, Parameters, Scoring, Workspace};
pub use compare::{compare_corpus, compare_directory, statistics, PairResult, Selection, Statistics};
pub use dna::{Dna, DnaCorpus, RawDna, TokenId, TokenInterner};
pub use format::format_g;
