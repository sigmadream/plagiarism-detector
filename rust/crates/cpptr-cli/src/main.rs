//! cpptr-cli: command line interface compatible with the C++ cpptr-cli.
//!
//! Comparison commands keep the C++ argument order and CSV format so existing benchmark scripts
//! work unchanged. DNA generation is not ported yet.

use std::fs::{self, File};
use std::io::{self, BufWriter, Write};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use cpptr_core::{compare_directory, format_g, statistics, Mode, PairResult, Parameters, Selection};

const VERSION: &str = env!("CARGO_PKG_VERSION");
const MANIFEST_HEADER: &str = "left_dna,right_dna,label,pair_id,kind,split";

fn print_usage(out: &mut dyn Write, program: &str) {
    let _ = write!(
        out,
        "cpptr-cli {VERSION} - Program DNA Plagiarism Detector (Rust)\n\
         \n\
         Usage: {program} <command> [args...] [--jobs <n>]\n\
         \n\
         Commands:\n\
         \x20 compare <dna_dir> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang> [--lines]\n\
         \x20          Compare all .DNA files in a directory and emit a CSV report\n\
         \x20 compare-manifest <dna_dir> <pairs.csv> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang> [--lines]\n\
         \x20          Compare only labeled pairs listed in a benchmark manifest\n\
         \n\
         Arguments:\n\
         \x20 <mode>        SC or FV\n\
         \x20 <lang>        C or CPP\n\
         \x20 --lines       Append the source line range of each aligned region to the CSV\n\
         \x20 --jobs <n>    Number of worker threads (default: all logical CPUs)\n\
         \n\
         Options:\n\
         \x20 --help       Display this help message\n\
         \x20 --version    Print the version and exit\n\
         \n\
         DNA generation (generate, generate-batch, similarity) is not ported yet; use the C++ tool.\n"
    );
}

/// A user-facing failure; the message is printed after "error: ".
struct Failure(String);

impl<E: std::fmt::Display> From<E> for Failure {
    fn from(error: E) -> Self {
        Failure(error.to_string())
    }
}

macro_rules! fail {
    ($($arg:tt)*) => { return Err(Failure(format!($($arg)*))) };
}

fn main() -> ExitCode {
    let mut args: Vec<String> = std::env::args().collect();
    let program = args.first().cloned().unwrap_or_else(|| "cpptr-cli".into());

    match run(&program, &mut args) {
        Ok(()) => ExitCode::SUCCESS,
        Err(Failure(message)) => {
            eprintln!("error: {message}");
            ExitCode::FAILURE
        }
    }
}

fn run(program: &str, args: &mut Vec<String>) -> Result<(), Failure> {
    take_jobs_option(args)?;

    let Some(command) = args.get(1).cloned() else {
        print_usage(&mut io::stderr(), program);
        fail!("missing command");
    };
    let rest = &args[2..];
    match command.as_str() {
        "--help" => {
            print_usage(&mut io::stdout(), program);
            Ok(())
        }
        "--version" => {
            println!("cpptr-cli {VERSION}");
            Ok(())
        }
        "compare" => handle_compare(rest),
        "compare-manifest" => handle_compare_manifest(rest),
        "generate" | "generate-batch" | "similarity" => {
            fail!("{command} is not ported to the Rust CLI yet; use the C++ cpptr-cli")
        }
        _ => {
            print_usage(&mut io::stderr(), program);
            fail!("unknown command {command}")
        }
    }
}

/// Removes `--jobs <n>` from anywhere in the arguments and sizes the global thread pool.
fn take_jobs_option(args: &mut Vec<String>) -> Result<(), Failure> {
    let Some(position) = args.iter().position(|arg| arg == "--jobs") else {
        return Ok(());
    };
    let Some(value) = args.get(position + 1) else {
        fail!("--jobs expects a thread count")
    };
    let jobs: usize = match value.parse() {
        Ok(jobs) if jobs > 0 => jobs,
        _ => fail!("--jobs must be a positive integer"),
    };
    args.drain(position..=position + 1);
    rayon::ThreadPoolBuilder::new().num_threads(jobs).build_global()?;
    Ok(())
}

/// Consumes a trailing `--lines` flag so the positional argument count stays unchanged.
fn take_lines_flag(args: &[String]) -> (&[String], bool) {
    match args.split_last() {
        Some((last, rest)) if last == "--lines" => (rest, true),
        _ => (args, false),
    }
}

fn parse_positive(value: &str) -> Option<f64> {
    value
        .trim_start()
        .parse::<f64>()
        .ok()
        .filter(|v| v.is_finite() && *v > 0.0)
}

struct Comparison {
    dna_directory: PathBuf,
    params: Parameters,
    output_path: PathBuf,
}

/// Parses `<alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang>`.
fn configure_comparison(
    dna_directory: &str,
    rest: &[String],
    output_file: &str,
) -> Result<Comparison, Failure> {
    let numbers: Vec<Option<f64>> = rest[..4].iter().map(|value| parse_positive(value)).collect();
    let [Some(alpha), Some(beta), Some(insertion), Some(deletion)] = numbers[..] else {
        fail!("alpha, beta, gamma, and delta must be positive finite numbers");
    };
    let Some(mode) = Mode::parse(&rest[4]) else {
        fail!("mode must be SC or FV")
    };
    if !matches!(rest[6].as_str(), "C" | "c" | "CPP" | "cpp") {
        fail!("language must be C or CPP; Java is not supported");
    }

    let dna_directory = PathBuf::from(dna_directory);
    if !dna_directory.is_dir() {
        fail!(
            "DNA directory does not exist or is not accessible: {}",
            dna_directory.display()
        );
    }
    let output_directory = PathBuf::from(&rest[5]);
    if let Err(error) = fs::create_dir_all(&output_directory) {
        fail!(
            "failed to create output directory {}: {error}",
            output_directory.display()
        );
    }

    Ok(Comparison {
        dna_directory,
        params: Parameters {
            alpha,
            beta,
            insertion,
            deletion,
            mode,
        },
        output_path: output_directory.join(output_file),
    })
}

fn handle_compare(args: &[String]) -> Result<(), Failure> {
    let (args, with_lines) = take_lines_flag(args);
    if args.len() != 8 {
        fail!("compare expects 8 arguments");
    }
    let comparison = configure_comparison(&args[0], &args[1..], "Plag-Detection-Result.csv")?;
    let results = compare_directory(
        &comparison.dna_directory,
        &comparison.params,
        &Selection::AllPairs,
    )?;
    print_statistics(&results);

    write_report(&comparison.output_path, |out| {
        write!(
            out,
            "Program1,Program2,SUM(P1:P2),Score,Begin_P1,End_P1,Begin_P2,End_P2"
        )?;
        write_line_header(out, with_lines)?;
        for result in &results {
            write_result(out, result, with_lines)?;
        }
        Ok(())
    })?;
    println!(
        "Comparison complete. Results written to {}",
        comparison.output_path.display()
    );
    Ok(())
}

struct ManifestRecord {
    left: String,
    right: String,
    label: String,
    pair_id: String,
    kind: String,
    split: String,
}

fn load_manifest(manifest: &Path, dna_directory: &Path) -> Result<Vec<ManifestRecord>, Failure> {
    let text = match fs::read_to_string(manifest) {
        Ok(text) => text,
        Err(error) => fail!("failed to open pair manifest {}: {error}", manifest.display()),
    };
    if text.is_empty() {
        fail!("pair manifest is empty");
    }
    let mut lines = text
        .split('\n')
        .map(|line| line.strip_suffix('\r').unwrap_or(line));
    if lines.next() != Some(MANIFEST_HEADER) {
        fail!("pair manifest has an unsupported header");
    }

    let mut records = Vec::new();
    for (index, line) in lines.enumerate() {
        let line_number = index + 2;
        if line.is_empty() {
            continue;
        }
        // std::getline splitting drops one trailing empty field; keep that behaviour.
        let mut fields: Vec<&str> = line.split(',').collect();
        if fields.last() == Some(&"") {
            fields.pop();
        }
        let [left, right, label, pair_id, kind, split] = fields[..] else {
            fail!("invalid pair manifest row {line_number}");
        };
        if left.is_empty() || right.is_empty() {
            fail!("invalid pair manifest row {line_number}");
        }
        if [left, right].iter().any(|name| name.contains(['/', '\\'])) {
            fail!("pair manifest DNA names must not contain directories");
        }
        if !dna_directory.join(left).is_file() || !dna_directory.join(right).is_file() {
            fail!("pair manifest row {line_number} references missing DNA");
        }
        records.push(ManifestRecord {
            left: left.into(),
            right: right.into(),
            label: label.into(),
            pair_id: pair_id.into(),
            kind: kind.into(),
            split: split.into(),
        });
    }
    if records.is_empty() {
        fail!("pair manifest contains no pairs");
    }
    Ok(records)
}

fn handle_compare_manifest(args: &[String]) -> Result<(), Failure> {
    let (args, with_lines) = take_lines_flag(args);
    if args.len() != 9 {
        fail!("compare-manifest expects 9 arguments");
    }
    let comparison = configure_comparison(&args[0], &args[2..], "Benchmark-Result.csv")?;
    let records = load_manifest(Path::new(&args[1]), &comparison.dna_directory)?;

    let selection = Selection::Pairs(
        records
            .iter()
            .map(|r| (r.left.clone(), r.right.clone()))
            .collect(),
    );
    let results = compare_directory(&comparison.dna_directory, &comparison.params, &selection)?;
    if results.len() != records.len() {
        fail!("one or more manifest DNA sequences are empty");
    }
    print_statistics(&results);

    write_report(&comparison.output_path, |out| {
        write!(
            out,
            "PairId,Label,Kind,Split,Program1,Program2,SUM(P1:P2),Score,Begin_P1,End_P1,Begin_P2,End_P2"
        )?;
        write_line_header(out, with_lines)?;
        for (record, result) in records.iter().zip(&results) {
            write!(
                out,
                "{},{},{},{},",
                record.pair_id, record.label, record.kind, record.split
            )?;
            write_result(out, result, with_lines)?;
        }
        Ok(())
    })?;
    println!(
        "Manifest comparison complete. Pairs: {}. Results written to {}",
        results.len(),
        comparison.output_path.display()
    );
    Ok(())
}

fn print_statistics(results: &[PairResult]) {
    if let Some(stats) = statistics(results) {
        println!("> AVRG = {}", format_g(stats.average));
        println!("> STDV = {}", format_g(stats.standard_deviation));
        println!("> BETA = {}", format_g(stats.beta));
        println!("> MU   = {}", format_g(stats.mu));
    }
}

fn write_report(
    path: &Path,
    body: impl FnOnce(&mut BufWriter<File>) -> io::Result<()>,
) -> Result<(), Failure> {
    let file = match File::create(path) {
        Ok(file) => file,
        Err(error) => fail!("failed to open output file {}: {error}", path.display()),
    };
    let mut out = BufWriter::new(file);
    if let Err(error) = body(&mut out).and_then(|()| out.flush()) {
        fail!("failed while writing output file {}: {error}", path.display());
    }
    Ok(())
}

fn write_line_header(out: &mut impl Write, with_lines: bool) -> io::Result<()> {
    if with_lines {
        write!(out, ",Line_Begin_P1,Line_End_P1,Line_Begin_P2,Line_End_P2")?;
    }
    writeln!(out)
}

fn write_result(out: &mut impl Write, result: &PairResult, with_lines: bool) -> io::Result<()> {
    let a = &result.alignment;
    write!(
        out,
        "{},{},{},{},{},{},{},{}",
        result.program1,
        result.program2,
        format_g(a.match_value_sum),
        format_g(a.similarity_percent),
        a.row_start,
        a.row_end,
        a.col_start,
        a.col_end
    )?;
    if with_lines {
        write!(
            out,
            ",{},{},{},{}",
            result.lines1.0, result.lines1.1, result.lines2.0, result.lines2.1
        )?;
    }
    writeln!(out)
}
