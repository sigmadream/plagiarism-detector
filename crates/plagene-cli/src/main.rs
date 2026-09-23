//! plagene: command line interface compatible with the C++ cpptr-cli.
//!
//! Commands keep the C++ argument order, DNA format and CSV format so existing benchmark scripts
//! work unchanged. DNA generation and pair comparison run in parallel.

use std::fs::{self, File};
use std::io::{self, BufWriter, Write};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use plagene_core::{
    compare_corpus, compare_directory, format_g, statistics, DnaCorpus, Mode, PairResult, Parameters, RawDna,
    Selection,
};
use plagene_frontend::{generate_dna, Config, DnaEvent, GenerateError};
use rayon::prelude::*;

const VERSION: &str = env!("CARGO_PKG_VERSION");
const MANIFEST_HEADER: &str = "left_dna,right_dna,label,pair_id,kind,split";

fn print_usage(out: &mut dyn Write, program: &str) {
    let _ = write!(
        out,
        "plagene {VERSION} - Program DNA plagiarism detector\n\
         \n\
         Usage: {program} <src_file1> <src_file2> [options]\n\
         \x20      {program} <command> [args...] [--jobs <n>]\n\
         \n\
         Default (no command): print the similarity of two C/C++ source files.\n\
         \x20 similarity <src_file1> <src_file2> [--config <ini>] [--mode SC|FV] [--lang C|CPP]\n\
         \x20            [--params <alpha> <beta> <gamma> <delta>] [--csv]\n\
         \x20          Defaults: built-in config, FV, language from file extension, params 1 1 1 1\n\
         \n\
         Commands:\n\
         \x20 generate <config.ini> <src_file> <dna_dir>\n\
         \x20          Generate .DNA file from source\n\
         \x20 generate-batch <config.ini> <src_dir> <dna_dir>\n\
         \x20          Generate .DNA files recursively in one process\n\
         \x20 compare <dna_dir> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang> [--lines]\n\
         \x20          Compare all .DNA files in a directory and emit a CSV report\n\
         \x20 compare-manifest <dna_dir> <pairs.csv> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang> [--lines]\n\
         \x20          Compare only labeled pairs listed in a benchmark manifest\n\
         \n\
         Arguments:\n\
         \x20 <config.ini>  Path to a config file, or '-' to use the built-in default config\n\
         \x20 <mode>        SC or FV\n\
         \x20 <lang>        C or CPP\n\
         \x20 --lines       Append the source line range of each aligned region to the CSV\n\
         \x20 --jobs <n>    Number of worker threads (default: all logical CPUs)\n\
         \n\
         Options:\n\
         \x20 --help       Display this help message\n\
         \x20 --version    Print the version and exit\n"
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
    // Show `plagene` in usage text rather than the full path it was started from.
    let program = args
        .first()
        .and_then(|arg0| Path::new(arg0).file_stem())
        .map_or_else(|| "plagene".into(), |stem| stem.to_string_lossy().into_owned());

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
            println!("plagene {VERSION}");
            Ok(())
        }
        "generate" => handle_generate(rest),
        "generate-batch" => handle_generate_batch(rest),
        "compare" => handle_compare(rest),
        "compare-manifest" => handle_compare_manifest(rest),
        "similarity" => handle_similarity(rest),
        // Default: the arguments are two source files to compare.
        _ => handle_similarity(&args[1..]),
    }
}

/// `-` selects the built-in configuration.
fn load_config(value: &str) -> Result<Config, Failure> {
    if value == "-" {
        Ok(Config::default())
    } else {
        Ok(Config::load(Path::new(value))?)
    }
}

fn handle_generate(args: &[String]) -> Result<(), Failure> {
    let [config, source, dna_directory] = args else {
        fail!("generate expects 3 arguments")
    };
    let config = load_config(config)?;
    let output = generate_dna(
        &*config.frontend(),
        Path::new(source),
        Path::new(dna_directory),
        true,
    )?;
    println!("{}", output.display());
    Ok(())
}

/// Extensions accepted by `generate-batch`, as in the C++ tool (headers are not scanned).
fn is_batch_source(path: &Path) -> bool {
    path.extension()
        .and_then(|ext| ext.to_str())
        .is_some_and(|ext| matches!(ext.to_ascii_lowercase().as_str(), "c" | "cc" | "cpp" | "cxx"))
}

fn collect_sources(directory: &Path, sources: &mut Vec<PathBuf>) -> io::Result<()> {
    for entry in fs::read_dir(directory)? {
        let entry = entry?;
        let file_type = entry.file_type()?;
        if file_type.is_dir() {
            collect_sources(&entry.path(), sources)?;
        } else if file_type.is_file() && is_batch_source(&entry.path()) {
            sources.push(entry.path());
        }
    }
    Ok(())
}

fn handle_generate_batch(args: &[String]) -> Result<(), Failure> {
    let [config, source_directory, dna_directory] = args else {
        fail!("generate-batch expects 3 arguments")
    };
    let config = load_config(config)?;
    let source_directory = Path::new(source_directory);
    let dna_directory = Path::new(dna_directory);
    if !source_directory.is_dir() {
        fail!(
            "source directory does not exist or is not accessible: {}",
            source_directory.display()
        );
    }

    let mut sources = Vec::new();
    collect_sources(source_directory, &mut sources)?;
    sources.sort();
    if sources.is_empty() {
        fail!("source directory contains no C/C++ files");
    }
    let mut names = std::collections::HashSet::new();
    for source in &sources {
        if !names.insert(source.file_name()) {
            fail!(
                "duplicate source filename would overwrite DNA output: {}",
                source.file_name().unwrap_or_default().to_string_lossy()
            );
        }
    }

    let frontend = config.frontend();
    let outcomes: Vec<Result<PathBuf, GenerateError>> = sources
        .par_iter()
        .map(|source| generate_dna(&*frontend, source, dna_directory, false))
        .collect();

    let mut failed = 0;
    for (source, outcome) in sources.iter().zip(&outcomes) {
        if let Err(error) = outcome {
            failed += 1;
            eprintln!("error: {}: {error}", source.display());
        }
    }
    println!(
        "DNA batch complete. Generated without source snapshots: {}, failed: {failed}",
        outcomes.len() - failed
    );
    if failed > 0 {
        fail!("{failed} source file(s) failed");
    }
    Ok(())
}

fn raw_dna(events: &[DnaEvent]) -> RawDna {
    RawDna {
        tokens: events.iter().map(|event| event.token.to_owned()).collect(),
        lines: events.iter().map(|event| event.line).collect(),
    }
}

/// similarity <src1> <src2> [--config <ini>] [--mode SC|FV] [--lang C|CPP] [--params a b g d] [--csv]
fn handle_similarity(args: &[String]) -> Result<(), Failure> {
    let mut sources: Vec<&str> = Vec::new();
    let mut config = Config::default();
    let mut params = Parameters {
        alpha: 1.0,
        beta: 1.0,
        insertion: 1.0,
        deletion: 1.0,
        mode: Mode::Frequency,
    };
    let mut language: Option<&str> = None;
    let mut csv = false;

    let mut index = 0;
    while index < args.len() {
        let arg = args[index].as_str();
        let value = args.get(index + 1).map(String::as_str);
        match (arg, value) {
            ("--config", Some(value)) => {
                config = load_config(value)?;
                index += 1;
            }
            ("--mode", Some(value)) => {
                let Some(mode) = Mode::parse(value) else {
                    fail!("--mode must be SC or FV")
                };
                params.mode = mode;
                index += 1;
            }
            ("--lang", Some(value)) => {
                language = Some(match value {
                    "C" | "c" => "C",
                    "CPP" | "cpp" => "CPP",
                    _ => fail!("--lang must be C or CPP"),
                });
                index += 1;
            }
            ("--params", _) if index + 4 < args.len() => {
                let mut values = [0.0; 4];
                for value in &mut values {
                    index += 1;
                    let Some(parsed) = parse_positive(&args[index]) else {
                        fail!("--params values must be positive finite numbers")
                    };
                    *value = parsed;
                }
                [params.alpha, params.beta, params.insertion, params.deletion] = values;
            }
            ("--csv", _) => csv = true,
            _ if arg.starts_with("--") => fail!("unknown option {arg}"),
            _ => sources.push(arg),
        }
        index += 1;
    }

    let [first, second] = sources[..] else {
        fail!("similarity expects exactly two source files")
    };
    for source in [first, second] {
        if !Path::new(source).is_file() {
            fail!("source file does not exist: {source}");
        }
    }
    let language = language.unwrap_or_else(|| {
        let is_c = |source: &str| {
            Path::new(source)
                .extension()
                .and_then(|ext| ext.to_str())
                .is_some_and(|ext| matches!(ext.to_ascii_lowercase().as_str(), "c" | "h"))
        };
        if is_c(first) && is_c(second) {
            "C"
        } else {
            "CPP"
        }
    });

    let frontend = config.frontend();
    let mut sequences = Vec::new();
    for (label, source) in [("A.DNA", first), ("B.DNA", second)] {
        let bytes = match fs::read(source) {
            Ok(bytes) => bytes,
            Err(_) => fail!("{source}: source file could not be opened for DNA generation"),
        };
        let events = frontend.scan(&bytes);
        if events.is_empty() {
            fail!("{source}: no DNA tokens were extracted from the source file");
        }
        sequences.push((label.to_owned(), raw_dna(&events)));
    }

    let corpus = DnaCorpus::from_raw(sequences);
    let results = compare_corpus(&corpus, corpus.len(), &params, &[(0, 1)]);
    let [result] = &results[..] else {
        fail!("one of the sources produced an empty DNA sequence")
    };
    let a = &result.alignment;

    if csv {
        println!(
            "Program1,Program2,Score,Begin_P1,End_P1,Begin_P2,End_P2,\
             Line_Begin_P1,Line_End_P1,Line_Begin_P2,Line_End_P2"
        );
        println!(
            "{first},{second},{},{},{},{},{},{},{},{},{}",
            format_g(a.similarity_percent),
            a.row_start,
            a.row_end,
            a.col_start,
            a.col_end,
            result.lines1.0,
            result.lines1.1,
            result.lines2.0,
            result.lines2.1
        );
    } else {
        println!("A: {first}");
        println!("B: {second}");
        println!(
            "similarity: {} ({}, {language})",
            format_g(a.similarity_percent),
            params.mode.label()
        );
        println!(
            "aligned region: A lines {}-{}, B lines {}-{}",
            result.lines1.0, result.lines1.1, result.lines2.0, result.lines2.1
        );
    }
    Ok(())
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
