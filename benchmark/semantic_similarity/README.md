# C++ semantic similarity benchmark

이 폴더는 Project CodeNet C++1400과 POJ-104를 같은 cppTR pipeline으로 평가한다.
두 corpus 모두 문제 ID를 정답 label의 proxy로 사용한다.

```text
positive = 같은 문제를 푼 서로 다른 C++ source
negative = 서로 다른 문제를 푼 크기가 비슷한 C++ source
```

이는 semantic similarity benchmark이며 실제 표절 정답이 아니다. 따라서 결과는
"표절 탐지 정확도"가 아니라 "같은 문제를 푼 프로그램의 식별 성능"으로 보고한다.

## 구성

```text
benchmark/semantic_similarity/
  benchmark.py          corpus 통합, pair 추출, materialize, 평가
  tests/                adapter와 metric 회귀 테스트
  work/                 생성 DB, source, DNA, 결과; Git 제외
```

`benchmark.py`는 두 corpus를 다음 공통 schema의 DuckDB로 정규화한다.

```text
corpus, split, problem_id, submission_id, source, source_bytes
```

- CodeNet은 `pNNNNN/*.cpp`의 problem/submission ID를 보존한다.
- POJ-104는 split마다 `id`가 재사용되므로 `split:id`를 submission ID로 사용한다.
- pair sampling은 한 split 안에서만 수행한다.
- negative pair는 source byte 크기가 가까운 다른 문제에서 선택한다.
- seed와 corpus가 같으면 같은 pair가 생성된다.

## 사전 준비

Python dependency는 저장소 root의 `pyproject.toml`에 정의되어 있다.

```powershell
uv sync
cmake -S src -B build/cpptr
cmake --build build/cpptr --config Debug
```

PowerShell 변수는 저장소 root에서 다음과 같이 설정한다.

```powershell
$tool = "benchmark/semantic_similarity/benchmark.py"
$cpptr = "build/cpptr/apps/cpptr-cli/Debug/cpptr-cli.exe"
$config = "src/resources/config/cconfig.ini"
$work = "benchmark/semantic_similarity/work"
```

필요한 입력은 다음 위치를 기준으로 설명한다.

```text
benchmark/Project_CodeNet_Cpp_1400/p00000/*.cpp
benchmark/POJ-104/data/{train,validation,test}-*.parquet
```

현재 저장소에는 POJ-104 Parquet만 있고 CodeNet C++1400 원본은 없다. CodeNet 단계는
공식 corpus를 위 경로에 배치한 후 실행해야 한다. C++1000을 사용할 때는 입력 및 출력
경로의 `1400`을 `1000`으로 바꾸며, 결과의 corpus 이름을 함께 기록한다.

## 공통 실험 조건

다음 값을 결과와 함께 고정해서 기록한다.

| 항목 | 값 |
|---|---:|
| random seed | `20260720` |
| alignment modes | `SC`, `FV` |
| cppTR numeric options | `1 1 1 1` |
| fixed thresholds | `50`, `70`, `90` |
| language | `CPP` |

CodeNet과 POJ의 문제 수가 다르므로 전체 pair 수를 같게 맞추지 않는다. 대신 각 문제에서
같은 수의 positive와 negative를 추출해 corpus 내부 class balance를 `1:1`로 유지한다.

## Project CodeNet C++1400

### 1. Corpus 통합

작은 source 파일을 DuckDB 하나로 통합한다. `--replace`는 기존 DB를 삭제한다.

```powershell
uv run python $tool bundle-codenet `
  benchmark/Project_CodeNet_Cpp_1400 `
  $work/codenet1400/corpus.duckdb `
  --replace
```

### 2. Pair 추출

문제마다 positive 10쌍과 negative 10쌍을 선택한다. 1,400개 문제가 모두 있으면 총
28,000쌍이 생성된다.

```powershell
uv run python $tool sample `
  $work/codenet1400/corpus.duckdb `
  --positive-per-problem 10 `
  --negative-per-problem 10 `
  --seed 20260720
```

### 3. Source와 manifest 생성

```powershell
uv run python $tool materialize `
  $work/codenet1400/corpus.duckdb `
  $work/codenet1400/evaluation `
  --replace
```

### 4. DNA 생성

```powershell
& $cpptr generate-batch `
  $config `
  $work/codenet1400/evaluation/sources `
  $work/codenet1400/evaluation/dna
```

`failed: 0`인지 확인한 뒤 비교를 진행한다.

### 5. SC와 FV 비교

```powershell
& $cpptr compare-manifest `
  $work/codenet1400/evaluation/dna `
  $work/codenet1400/evaluation/pairs.csv `
  1 1 1 1 SC `
  $work/codenet1400/evaluation/results-sc `
  CPP

& $cpptr compare-manifest `
  $work/codenet1400/evaluation/dna `
  $work/codenet1400/evaluation/pairs.csv `
  1 1 1 1 FV `
  $work/codenet1400/evaluation/results-fv `
  CPP
```

### 6. 지표 계산

```powershell
uv run python $tool evaluate `
  $work/codenet1400/evaluation/results-sc/Benchmark-Result.csv `
  --output $work/codenet1400/evaluation/results-sc/metrics.json

uv run python $tool evaluate `
  $work/codenet1400/evaluation/results-fv/Benchmark-Result.csv `
  --output $work/codenet1400/evaluation/results-fv/metrics.json
```

CodeNet에는 이 pipeline에서 사용하는 별도 test split이 없다. `best_f1_exploratory`는
동일 데이터에서 선택한 탐색값이므로 일반화 성능으로 보고하지 않는다. SC/FV 비교에는
threshold-free 지표인 ROC-AUC와 PR-AUC를 우선 사용한다.

## POJ-104

현재 Parquet는 104개 문제와 53,000개 source를 포함한다.

| split | source | 문제 |
|---|---:|---:|
| train | 32,500 | 65 |
| validation | 8,500 | 17 |
| test | 12,000 | 24 |

### 1. Corpus 통합

```powershell
uv run python $tool bundle-poj `
  benchmark/POJ-104/data `
  $work/poj104/corpus.duckdb `
  --replace
```

### 2. Validation pilot

validation에서 문제마다 positive/negative 20쌍을 추출한다. 여기서 SC/FV 설정과
운영 threshold를 선택하고, test 결과를 보기 전에 threshold를 고정한다.

```powershell
uv run python $tool sample `
  $work/poj104/corpus.duckdb `
  --split validation `
  --positive-per-problem 20 `
  --negative-per-problem 20 `
  --seed 20260720

uv run python $tool materialize `
  $work/poj104/corpus.duckdb `
  $work/poj104/validation `
  --replace
```

위 CodeNet 절차와 동일하게 `generate-batch`, SC/FV `compare-manifest`, `evaluate`를
`$work/poj104/validation`에 실행한다. `best_f1_exploratory.threshold`를 pilot threshold
후보로 기록한다.

### 3. Test pair 추출

`sample`은 DB의 기존 pair를 교체하지만 이미 materialize한 validation 데이터에는 영향을
주지 않는다. test의 24개 문제에서 문제마다 positive/negative 100쌍, 총 4,800쌍을 만든다.

```powershell
uv run python $tool sample `
  $work/poj104/corpus.duckdb `
  --split test `
  --positive-per-problem 100 `
  --negative-per-problem 100 `
  --seed 20260720

uv run python $tool materialize `
  $work/poj104/corpus.duckdb `
  $work/poj104/test `
  --replace
```

### 4. Test 실행

```powershell
& $cpptr generate-batch `
  $config `
  $work/poj104/test/sources `
  $work/poj104/test/dna

& $cpptr compare-manifest `
  $work/poj104/test/dna `
  $work/poj104/test/pairs.csv `
  1 1 1 1 SC `
  $work/poj104/test/results-sc `
  CPP

& $cpptr compare-manifest `
  $work/poj104/test/dna `
  $work/poj104/test/pairs.csv `
  1 1 1 1 FV `
  $work/poj104/test/results-fv `
  CPP
```

validation의 `best_f1_exploratory.threshold`를 mode별로 기록하고 test 결과를 보기 전에
고정한다. 예를 들어 다음 변수에 validation 결과를 설정한다.

```powershell
$scThreshold = 13.5593
$fvThreshold = 20.2005

uv run python $tool evaluate `
  $work/poj104/test/results-sc/Benchmark-Result.csv `
  --threshold $scThreshold `
  --output $work/poj104/test/results-sc/metrics.json

uv run python $tool evaluate `
  $work/poj104/test/results-fv/Benchmark-Result.csv `
  --threshold $fvThreshold `
  --output $work/poj104/test/results-fv/metrics.json
```

`best_f1_exploratory`는 test 결과 해석에 사용하지 않는다. 고정 threshold의 precision,
recall, F1과 ROC-AUC, PR-AUC를 최종값으로 사용한다.

## 상태 확인

DB의 source, split, pair 수는 언제든 확인할 수 있다.

```powershell
uv run python $tool status $work/codenet1400/corpus.duckdb
uv run python $tool status $work/poj104/corpus.duckdb
```

## 결과 항목

`metrics.json`은 다음 항목을 기록한다.

- `roc_auc`: positive score가 negative보다 높을 확률에 해당하는 ROC-AUC
- `pr_auc_average_precision`: 동점 score를 한 threshold group으로 처리한 average precision
- `requested_thresholds`: 고정 threshold별 TP, FP, FN, TN, precision, recall, F1
- `best_f1_exploratory`: 현재 결과에서 F1이 가장 높은 탐색 threshold

최종 표에는 corpus, split, pair 수, mode, ROC-AUC, PR-AUC, 고정 threshold의 precision,
recall, F1을 함께 기록한다. 서로 다른 corpus에서 따로 최적화한 threshold의 F1만 직접
비교하지 않는다.

## 실행시간 측정

확장성 결과는 정확도 지표와 분리한다. 동일한 PC에서 다른 부하를 종료한 후 각 명령을
`Measure-Command`로 감싸 DNA 생성시간과 pair 비교시간을 따로 기록한다.

```powershell
$dnaTime = Measure-Command {
  & $cpptr generate-batch $config <sources> <dna>
}

$compareTime = Measure-Command {
  & $cpptr compare-manifest <dna> <pairs.csv> 1 1 1 1 FV <results> CPP
}

$dnaTime.TotalSeconds
$compareTime.TotalSeconds
```

보고서에는 CPU, RAM, OS, build configuration, source 수, DNA 수, pair 수를 함께 남긴다.

## 테스트

```powershell
uv run python -m unittest discover `
  -s benchmark/semantic_similarity/tests `
  -v
```

테스트는 두 corpus adapter, split-qualified POJ ID, deterministic balanced manifest, ROC-AUC,
동점에 안전한 PR-AUC 계산을 검증한다.

현재 저장소에서 실행한 baseline은 [RESULTS.md](RESULTS.md)에 기록한다.