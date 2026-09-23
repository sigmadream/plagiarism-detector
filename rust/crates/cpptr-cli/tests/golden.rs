//! Golden tests: the Rust CLI must reproduce reports written by the C++ cpptr-cli 0.2.0
//! (fixtures/poj-small/expected) line for line.

use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

fn fixture() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixtures/poj-small")
}

fn output_dir(name: &str) -> PathBuf {
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join(name);
    let _ = fs::remove_dir_all(&dir);
    dir
}

fn run(args: &[&str]) {
    let output = Command::new(env!("CARGO_BIN_EXE_cpptr-cli"))
        .args(args)
        .output()
        .expect("cpptr-cli runs");
    assert!(
        output.status.success(),
        "cpptr-cli {args:?} failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
}

fn assert_same_lines(actual: &Path, expected: &Path) {
    let actual = fs::read_to_string(actual).expect("report written");
    let expected = fs::read_to_string(expected).expect("expected report");
    let actual: Vec<&str> = actual.lines().collect();
    let expected: Vec<&str> = expected.lines().collect();
    assert_eq!(actual.len(), expected.len(), "row count");
    for (index, (a, e)) in actual.iter().zip(&expected).enumerate() {
        assert_eq!(a, e, "row {index}");
    }
}

#[test]
fn compare_all_pairs_with_lines_matches_cpp() {
    let dna = fixture().join("dna");
    for mode in ["SC", "FV"] {
        let out = output_dir(&format!("compare-{mode}"));
        run(&[
            "compare",
            dna.to_str().unwrap(),
            "1",
            "1",
            "1",
            "1",
            mode,
            out.to_str().unwrap(),
            "CPP",
            "--lines",
        ]);
        assert_same_lines(
            &out.join("Plag-Detection-Result.csv"),
            &fixture().join(format!("expected/compare-{}-lines.csv", mode.to_lowercase())),
        );
    }
}

#[test]
fn compare_manifest_matches_cpp() {
    let dna = fixture().join("dna");
    let pairs = fixture().join("pairs.csv");
    for mode in ["SC", "FV"] {
        let out = output_dir(&format!("manifest-{mode}"));
        run(&[
            "compare-manifest",
            dna.to_str().unwrap(),
            pairs.to_str().unwrap(),
            "1",
            "1",
            "1",
            "1",
            mode,
            out.to_str().unwrap(),
            "CPP",
            "--jobs",
            "2",
        ]);
        assert_same_lines(
            &out.join("Benchmark-Result.csv"),
            &fixture().join(format!("expected/manifest-{}.csv", mode.to_lowercase())),
        );
    }
}
