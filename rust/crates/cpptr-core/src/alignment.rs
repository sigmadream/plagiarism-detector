//! Smith-Waterman local alignment with PintCon SC/FV scoring.
//!
//! The arithmetic mirrors the C++ `alignment_engine` operation for operation (same expression
//! order and tie-breaking), so scores are bit-identical to the reference implementation.

use crate::dna::TokenId;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Mode {
    /// SC: fixed match, mismatch and gap scores.
    Score,
    /// FV: scores weighted by corpus token frequencies.
    Frequency,
}

impl Mode {
    pub fn parse(value: &str) -> Option<Self> {
        match value {
            "SC" | "sc" => Some(Mode::Score),
            "FV" | "fv" => Some(Mode::Frequency),
            _ => None,
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Mode::Score => "SC",
            Mode::Frequency => "FV",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Parameters {
    pub alpha: f64,
    pub beta: f64,
    pub insertion: f64,
    pub deletion: f64,
    pub mode: Mode,
}

impl Default for Parameters {
    fn default() -> Self {
        Parameters {
            alpha: 0.5,
            beta: 0.5,
            insertion: 4.0,
            deletion: 4.0,
            mode: Mode::Frequency,
        }
    }
}

/// Frequency used when a token has none (or a non-positive one), as in the C++ engine.
const MISSING_FREQUENCY: f64 = 0.0001;

fn log2_safe(x: f64) -> f64 {
    if x <= 0.0 {
        0.0
    } else {
        x.log2()
    }
}

/// Per-token scores precomputed for one vocabulary, frequency table and parameter set.
#[derive(Debug, Clone)]
pub struct Scoring {
    vocabulary: usize,
    matched: Vec<f64>,
    mismatched: Vec<f64>, // vocabulary x vocabulary, row-major
    deletion_gap: Vec<f64>,
    insertion_gap: Vec<f64>,
}

impl Scoring {
    /// `frequencies[token]` is the corpus frequency of the token in percent.
    pub fn new(params: &Parameters, frequencies: &[Option<f64>]) -> Self {
        let vocabulary = frequencies.len();
        let fv: Vec<f64> = frequencies
            .iter()
            .map(|frequency| {
                let value = frequency.map_or(MISSING_FREQUENCY, |percent| percent * 0.01);
                if value <= 0.0 {
                    MISSING_FREQUENCY
                } else {
                    value
                }
            })
            .collect();

        let (matched, mismatched, deletion_gap, insertion_gap) = match params.mode {
            Mode::Score => (
                vec![params.alpha; vocabulary],
                vec![-params.beta; vocabulary * vocabulary],
                vec![-params.deletion; vocabulary],
                vec![-params.insertion; vocabulary],
            ),
            Mode::Frequency => {
                // PintCon: match = -alpha * log2(p^2), mismatch = beta * log2(p * q),
                // gap = penalty * log2(p).
                let matched = fv.iter().map(|&p| -params.alpha * log2_safe(p * p)).collect();
                let mut mismatched = Vec::with_capacity(vocabulary * vocabulary);
                for &p in &fv {
                    for &q in &fv {
                        mismatched.push(params.beta * log2_safe(p * q));
                    }
                }
                let deletion_gap = fv.iter().map(|&p| params.deletion * log2_safe(p)).collect();
                let insertion_gap = fv.iter().map(|&p| params.insertion * log2_safe(p)).collect();
                (matched, mismatched, deletion_gap, insertion_gap)
            }
        };
        Scoring {
            vocabulary,
            matched,
            mismatched,
            deletion_gap,
            insertion_gap,
        }
    }

    #[inline]
    fn substitution(&self, a: TokenId, b: TokenId) -> f64 {
        if a == b {
            self.matched[a as usize]
        } else {
            self.mismatched[a as usize * self.vocabulary + b as usize]
        }
    }

    pub fn match_score(&self, token: TokenId) -> f64 {
        self.matched[token as usize]
    }
}

/// Best local alignment between two sequences.
///
/// `row_*` index the first sequence and `col_*` the second. As in the C++ engine, `*_start` is
/// the table index where the trace-back stopped (0-based first aligned token) and `*_end` is the
/// table index of the maximum cell (1-based last aligned token).
#[derive(Debug, Default, Clone, PartialEq)]
pub struct Alignment {
    pub similarity_percent: f64,
    pub match_value_sum: f64,
    pub shorter_dna_score: f64,
    pub row_start: usize,
    pub row_end: usize,
    pub col_start: usize,
    pub col_end: usize,
}

const ZERO: u8 = 0;
const UP: u8 = 1;
const LEFT: u8 = 2;
const DIAGONAL: u8 = 3;

/// Reusable buffers so a worker thread allocates once for many comparisons.
#[derive(Debug, Default)]
pub struct Workspace {
    arrows: Vec<u8>,
    previous: Vec<f64>,
    current: Vec<f64>,
}

pub fn align(first: &[TokenId], second: &[TokenId], scoring: &Scoring, work: &mut Workspace) -> Alignment {
    let rows = first.len();
    let cols = second.len();
    let width = cols + 1;

    work.arrows.clear();
    work.arrows.resize((rows + 1) * width, ZERO);
    work.previous.clear();
    work.previous.resize(width, 0.0);
    work.current.clear();
    work.current.resize(width, 0.0);

    // The C++ engine scans the finished table row-major for the first strict maximum;
    // tracking it during the fill visits cells in the same order.
    let mut max_score = -1.0;
    let (mut max_r, mut max_c) = (0, 0);

    for i in 1..=rows {
        let a = first[i - 1];
        let up_gap = scoring.deletion_gap[a as usize];
        let arrow_row = &mut work.arrows[i * width..(i + 1) * width];
        work.current[0] = 0.0;
        for j in 1..=cols {
            let b = second[j - 1];
            let diag_score = work.previous[j - 1] + scoring.substitution(a, b);
            let up_score = work.previous[j] + up_gap;
            let left_score = work.current[j - 1] + scoring.insertion_gap[b as usize];

            let (score, arrow) = if diag_score >= up_score && diag_score >= left_score && diag_score > 0.0 {
                (diag_score, DIAGONAL)
            } else if up_score >= left_score && up_score > 0.0 {
                (up_score, UP)
            } else if left_score > 0.0 {
                (left_score, LEFT)
            } else {
                (0.0, ZERO)
            };
            work.current[j] = score;
            arrow_row[j] = arrow;

            if score > max_score {
                max_score = score;
                max_r = i;
                max_c = j;
            }
        }
        std::mem::swap(&mut work.previous, &mut work.current);
    }

    if max_score <= 0.0 {
        return Alignment::default();
    }

    let (mut r, mut c) = (max_r, max_c);
    while r > 0 && c > 0 {
        match work.arrows[r * width + c] {
            DIAGONAL => {
                r -= 1;
                c -= 1;
            }
            UP => r -= 1,
            LEFT => c -= 1,
            _ => break,
        }
    }

    let shorter = if first.len() < second.len() { first } else { second };
    let mut shorter_score = 0.0;
    for &token in shorter {
        shorter_score += scoring.match_score(token);
    }

    let similarity_percent = if shorter_score > 0.0 {
        (max_score / shorter_score) * 100.0
    } else {
        100.0 // unreachable in practice: a positive alignment implies a positive denominator
    };

    Alignment {
        similarity_percent,
        match_value_sum: max_score,
        shorter_dna_score: shorter_score,
        row_start: r,
        row_end: max_r,
        col_start: c,
        col_end: max_c,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sc() -> Scoring {
        let params = Parameters {
            alpha: 1.0,
            beta: 1.0,
            insertion: 1.0,
            deletion: 1.0,
            mode: Mode::Score,
        };
        Scoring::new(&params, &[Some(25.0); 4])
    }

    #[test]
    fn identical_sequences_score_one_hundred() {
        let mut work = Workspace::default();
        let result = align(&[0, 1, 2, 3], &[0, 1, 2, 3], &sc(), &mut work);
        assert_eq!(result.similarity_percent, 100.0);
        assert_eq!((result.row_start, result.row_end), (0, 4));
        assert_eq!((result.col_start, result.col_end), (0, 4));
    }

    #[test]
    fn local_region_is_found_inside_noise() {
        let mut work = Workspace::default();
        let result = align(&[3, 3, 0, 1, 2, 3, 3], &[0, 1, 2], &sc(), &mut work);
        assert_eq!(result.match_value_sum, 3.0);
        assert_eq!((result.row_start, result.row_end), (2, 5));
        assert_eq!((result.col_start, result.col_end), (0, 3));
    }

    #[test]
    fn disjoint_sequences_have_no_alignment() {
        let mut work = Workspace::default();
        let result = align(&[0, 0], &[1, 1], &sc(), &mut work);
        assert_eq!(result, Alignment::default());
    }
}
