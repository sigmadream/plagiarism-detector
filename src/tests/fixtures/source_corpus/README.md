# Source Corpus Fixtures

This directory contains source files used as inputs for cppTR regression and smoke tests. It is test data, not legacy runtime code or a production resource.

## Contents

- `c/`: C source corpus consumed by `cpptr_generate_c_corpus_smoke`. The test verifies that DNA and source snapshots can be generated across the corpus and allows at most one unsupported input.
- `cpp/`: Two C++ samples used by DNA generation, CLI, and CSV comparison tests.

The numeric filenames are retained from the original source corpus so existing test cases remain traceable. These files do not include ground-truth clone labels and should not be treated as a precision/recall benchmark.