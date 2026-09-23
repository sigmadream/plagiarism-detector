//! Pairwise comparison of a DNA corpus, parallelised across pairs.

use std::io;
use std::path::Path;

use rayon::prelude::*;

use crate::alignment::{align, Alignment, Parameters, Scoring, Workspace};
use crate::dna::DnaCorpus;

/// One compared pair with its alignment and the source line span of the aligned region.
#[derive(Debug, Clone, PartialEq)]
pub struct PairResult {
    pub program1: String,
    pub program2: String,
    pub alignment: Alignment,
    /// Source line range (first, last) of the aligned tokens; 0 when unknown.
    pub lines1: (u32, u32),
    pub lines2: (u32, u32),
}

/// Pairs to compare, by DNA file name.
#[derive(Debug, Clone)]
pub enum Selection {
    /// Every unordered pair of `.DNA` files in the directory, in sorted file-name order.
    AllPairs,
    /// Exactly these pairs, in this order.
    Pairs(Vec<(String, String)>),
}

/// Compares DNA files of `directory`.
///
/// Token frequencies are always computed over every `.DNA` file in the directory, even when only
/// a subset of pairs is compared. The result keeps the selection order; a pair is skipped when
/// either sequence is empty.
pub fn compare_directory(
    directory: &Path,
    params: &Parameters,
    selection: &Selection,
) -> io::Result<Vec<PairResult>> {
    let mut corpus = DnaCorpus::load_directory(directory)?;
    let scanned = corpus.len();

    let pairs: Vec<(usize, usize)> = match selection {
        Selection::AllPairs => (0..scanned)
            .flat_map(|i| (i + 1..scanned).map(move |j| (i, j)))
            .collect(),
        Selection::Pairs(names) => names
            .iter()
            .map(|(left, right)| {
                Ok((
                    corpus.resolve(directory, left)?,
                    corpus.resolve(directory, right)?,
                ))
            })
            .collect::<io::Result<_>>()?,
    };

    Ok(compare_corpus(&corpus, scanned, params, &pairs))
}

/// Compares `pairs` of corpus positions in parallel, keeping their order. Token frequencies come
/// from the first `scanned` sequences. A pair is skipped when either sequence is empty.
pub fn compare_corpus(
    corpus: &DnaCorpus,
    scanned: usize,
    params: &Parameters,
    pairs: &[(usize, usize)],
) -> Vec<PairResult> {
    let scoring = Scoring::new(params, &corpus.frequencies(scanned));

    let results = pairs
        .par_iter()
        .map_init(Workspace::default, |work, &(left, right)| {
            let first = corpus.dna(left);
            let second = corpus.dna(right);
            if first.tokens.is_empty() || second.tokens.is_empty() {
                return None;
            }
            let alignment = align(&first.tokens, &second.tokens, &scoring, work);
            Some(PairResult {
                program1: corpus.name(left).to_owned(),
                program2: corpus.name(right).to_owned(),
                lines1: line_span(&first.lines, alignment.row_start, alignment.row_end),
                lines2: line_span(&second.lines, alignment.col_start, alignment.col_end),
                alignment,
            })
        })
        .collect::<Vec<_>>();

    results.into_iter().flatten().collect()
}

/// Source line span of the tokens with indices `begin..=end`, clamped to the sequence.
/// Matches the C++ report, including its use of the raw table indices as token indices.
pub fn line_span(lines: &[u32], begin: usize, end: usize) -> (u32, u32) {
    let Some(last) = lines.len().checked_sub(1) else {
        return (0, 0);
    };
    let begin = begin.min(last);
    let end = end.clamp(begin, last);
    let mut span = (0, 0);
    for &line in &lines[begin..=end] {
        if line == 0 {
            continue;
        }
        if span.0 == 0 || line < span.0 {
            span.0 = line;
        }
        if line > span.1 {
            span.1 = line;
        }
    }
    span
}

/// Summary of the score distribution printed by the `compare` commands.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Statistics {
    pub average: f64,
    pub standard_deviation: f64,
    /// Gumbel scale estimated by the method of moments.
    pub beta: f64,
    /// Gumbel location estimated by the method of moments.
    pub mu: f64,
}

pub fn statistics(results: &[PairResult]) -> Option<Statistics> {
    if results.is_empty() {
        return None;
    }
    let count = results.len() as f64;
    let average = results
        .iter()
        .map(|r| r.alignment.similarity_percent)
        .sum::<f64>()
        / count;
    let squares: f64 = results
        .iter()
        .map(|r| (r.alignment.similarity_percent - average).powi(2))
        .sum();
    let standard_deviation = (squares / count).sqrt();
    let beta = standard_deviation * 6.0_f64.sqrt() / std::f64::consts::PI;
    let mu = average - 0.57721 * beta;
    Some(Statistics {
        average,
        standard_deviation,
        beta,
        mu,
    })
}

#[cfg(test)]
mod tests {
    use super::line_span;

    #[test]
    fn line_span_ignores_unknown_lines_and_clamps() {
        assert_eq!(line_span(&[], 0, 3), (0, 0));
        assert_eq!(line_span(&[3, 0, 5, 4], 0, 3), (3, 5));
        assert_eq!(line_span(&[3, 0, 5, 4], 1, 99), (4, 5));
        assert_eq!(line_span(&[3, 0, 5, 4], 9, 9), (4, 4));
    }
}
