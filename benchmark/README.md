# POJ-104 benchmark

이 폴더는 POJ-104로 cppTR의 SC/FV 모드를 평가하고 같은 데이터에서 JPlag와 비교한다.
POJ-104는 문제 ID를 정답 label의 proxy로 사용한다.

```text
positive = 같은 문제를 푼 서로 다른 C++ source
negative = 서로 다른 문제를 푼 크기가 비슷한 C++ source
```

이는 semantic similarity benchmark이며 실제 표절 정답이 아니다. 따라서 결과는
"표절 탐지 정확도"가 아니라 "같은 문제를 푼 프로그램의 식별 성능"으로 보고한다.

## 구성

```text
benchmark/
  benchmark.py      POJ-104 통합, pair 추출, materialize, 지표 계산
  bootstrap.py      문제 단위 paired bootstrap 95% CI
  jplag.py          같은 manifest에 JPlag 실행 후 결과를 cppTR 형식으로 변환
  tests/            adapter와 metric 회귀 테스트
  POJ-104/          {train,validation,test}-00000-of-00001.parquet (Git 제외)
  jplag-6.3.0-jar-with-dependencies.jar (Git 제외)
  work/             생성 DB, source, DNA, 결과 (Git 제외)
  RESULTS.md        실행 결과
```

`benchmark.py`는 Parquet를 다음 schema의 DuckDB로 정규화한다.

```text
corpus, split, problem_id, submission_id, source, source_bytes
```

- POJ-104는 split마다 `id`가 재사용되므로 `split:id`를 submission ID로 사용한다.
- pair sampling은 한 split 안에서만 수행한다.
- negative pair는 source byte 크기가 가까운 다른 문제에서 선택한다.
- seed가 같으면 같은 pair가 생성된다.

## 사전 준비

- POJ-104 Parquet 3개를 `benchmark/POJ-104/`에 둔다. 출처는 HuggingFace
  `google/code_x_glue_cc_clone_detection_poj104`의 `data/` 폴더다.
- JPlag 6.3.0 jar를 `benchmark/`에 두고 Java 25 실행 파일 경로를 준비한다. JPlag 6.3.0은
  Java 25 미만에서 실행되지 않는다.

```powershell
uv sync
cmake -S src -B build/cpptr
cmake --build build/cpptr --config Debug
```

PowerShell 변수는 저장소 root에서 다음과 같이 설정한다.

```powershell
$tool = "benchmark/benchmark.py"
$cpptr = "build/cpptr/apps/cpptr-cli/Debug/cpptr-cli.exe"
$config = "src/resources/config/cconfig.ini"
$work = "benchmark/work/poj104"
$java = "<java 25 실행 파일>"
```

현재 Parquet는 104개 문제와 53,000개 source를 포함한다.

| split | source | 문제 |
|---|---:|---:|
| train | 32,500 | 65 |
| validation | 8,500 | 17 |
| test | 12,000 | 24 |

## 공통 실험 조건

다음 값을 결과와 함께 고정해서 기록한다.

| 항목 | 값 |
|---|---:|
| random seed | `20260720` |
| alignment modes | `SC`, `FV` |
| cppTR numeric options | `1 1 1 1` |
| language | `CPP` |
| JPlag | 6.3.0, `-l cpp`, 기본 min-tokens |

문제마다 같은 수의 positive와 negative를 추출해 class balance를 `1:1`로 유지한다.

## 1. Corpus 통합

```powershell
uv run python $tool bundle-poj benchmark/POJ-104 $work/corpus.duckdb --replace
```

## 2. Validation pilot

validation에서 문제마다 positive/negative 20쌍(총 680쌍)을 추출한다. 여기서 mode별 운영
threshold를 선택하고, test 결과를 보기 전에 threshold를 고정한다.

```powershell
uv run python $tool sample $work/corpus.duckdb `
  --split validation --positive-per-problem 20 --negative-per-problem 20 --seed 20260720
uv run python $tool materialize $work/corpus.duckdb $work/validation --replace

& $cpptr generate-batch $config $work/validation/sources $work/validation/dna
& $cpptr compare-manifest $work/validation/dna $work/validation/pairs.csv 1 1 1 1 SC $work/validation/results-sc CPP
& $cpptr compare-manifest $work/validation/dna $work/validation/pairs.csv 1 1 1 1 FV $work/validation/results-fv CPP

uv run python $tool evaluate $work/validation/results-sc/Benchmark-Result.csv --output $work/validation/results-sc/metrics.json
uv run python $tool evaluate $work/validation/results-fv/Benchmark-Result.csv --output $work/validation/results-fv/metrics.json
```

`generate-batch`가 `failed: 0`인지 확인한 뒤 비교를 진행한다. 각 mode의
`best_f1_exploratory.threshold`를 pilot threshold로 기록한다.

## 3. Test

`sample`은 DB의 기존 pair를 교체하지만 이미 materialize한 validation 데이터에는 영향을
주지 않는다. test의 24개 문제에서 문제마다 positive/negative 100쌍, 총 4,800쌍을 만든다.

```powershell
uv run python $tool sample $work/corpus.duckdb `
  --split test --positive-per-problem 100 --negative-per-problem 100 --seed 20260720
uv run python $tool materialize $work/corpus.duckdb $work/test --replace

& $cpptr generate-batch $config $work/test/sources $work/test/dna
& $cpptr compare-manifest $work/test/dna $work/test/pairs.csv 1 1 1 1 SC $work/test/results-sc CPP
& $cpptr compare-manifest $work/test/dna $work/test/pairs.csv 1 1 1 1 FV $work/test/results-fv CPP
```

validation에서 기록한 threshold를 고정해 평가한다. 현재 스캐너의 값은 SC 9.375, FV 10.472다.

```powershell
uv run python $tool evaluate $work/test/results-sc/Benchmark-Result.csv `
  --threshold 9.375 --output $work/test/results-sc/metrics.json
uv run python $tool evaluate $work/test/results-fv/Benchmark-Result.csv `
  --threshold 10.472 --output $work/test/results-fv/metrics.json
```

`evaluate`는 `--threshold`를 주지 않으면 50, 70, 90을 사용한다. `best_f1_exploratory`는 test
결과 해석에 사용하지 않는다. 고정 threshold의 precision, recall, F1과 ROC-AUC, PR-AUC를
최종값으로 사용한다.

## 4. JPlag baseline

같은 materialized source와 manifest에 JPlag를 실행해 pair별 average similarity x 100을
`Score`로 기록한다.

```powershell
uv run python benchmark/jplag.py run $work/validation/sources $work/validation/pairs.csv `
  $work/validation/results-jplag --java $java --chunk-files 150
uv run python benchmark/jplag.py run $work/test/sources $work/test/pairs.csv `
  $work/test/results-jplag --java $java --chunk-files 150

uv run python $tool evaluate $work/test/results-jplag/Benchmark-Result.csv `
  --threshold 50 --output $work/test/results-jplag/metrics.json
```

`--chunk-files`는 manifest pair를 최대 150개 파일 묶음으로 나눠 JPlag를 반복 실행한다.
JPlag의 pair 유사도는 다른 제출물에 영향을 받지 않으므로 결과는 전체 실행과 같고, 전체
조합 비교를 피해 실행시간이 약 5배 줄어든다(test 6,553개 source 기준 약 11분). JPlag가 토큰
수 부족으로 비교하지 않은 pair는 `Missing=1`, `Score=0`으로 기록되며 개수는 `summary.json`에
남는다. source, 출력 디렉터리, 작업 디렉터리는 같은 드라이브에 두어야 한다. JPlag의 report
writer가 드라이브가 다르면 예외를 낸다.

## 5. 문제 단위 bootstrap 신뢰구간

pair는 문제와 source를 공유하므로 pair 행을 독립 관측치로 재표본화하지 않는다.
`bootstrap.py`는 `Program1` 이름에서 문제 ID를 읽어 문제를 cluster로 삼는 paired bootstrap을
수행하고, 같은 재표본을 모든 system에 적용해 지표 차이의 95% percentile CI를 계산한다.

```powershell
uv run python benchmark/bootstrap.py `
  --system sc=$work/test/results-sc/Benchmark-Result.csv `
  --system fv=$work/test/results-fv/Benchmark-Result.csv `
  --system jplag=$work/test/results-jplag/Benchmark-Result.csv `
  --threshold sc=9.375 --threshold fv=10.472 --threshold jplag=50 `
  --iterations 10000 --seed 20260720 `
  --output $work/test/metrics-bootstrap.json
```

출력의 `differences.<a>-<b>.<metric>.ci_excludes_zero`가 true이면 두 system의 차이가 95%
CI에서 0을 포함하지 않는다. 10,000회 반복은 4,800쌍 3개 system 기준 약 10분 걸린다.

## 상태 확인

```powershell
uv run python $tool status $work/corpus.duckdb
```

## 결과 항목

`metrics.json`은 다음 항목을 기록한다.

- `roc_auc`: positive score가 negative보다 높을 확률에 해당하는 ROC-AUC
- `pr_auc_average_precision`: 동점 score를 한 threshold group으로 처리한 average precision
- `requested_thresholds`: 고정 threshold별 TP, FP, FN, TN, precision, recall, F1
- `best_f1_exploratory`: 현재 결과에서 F1이 가장 높은 탐색 threshold

최종 표에는 split, pair 수, system, ROC-AUC, PR-AUC, 고정 threshold의 precision, recall,
F1을 함께 기록한다.

## 실행시간 측정

확장성 결과는 정확도 지표와 분리한다. 동일한 PC에서 다른 부하를 종료한 후 각 명령을
`Measure-Command`로 감싸 DNA 생성시간과 pair 비교시간을 따로 기록한다.

```powershell
$dnaTime = Measure-Command { & $cpptr generate-batch $config <sources> <dna> }
$compareTime = Measure-Command { & $cpptr compare-manifest <dna> <pairs.csv> 1 1 1 1 FV <results> CPP }
$dnaTime.TotalSeconds
$compareTime.TotalSeconds
```

보고서에는 CPU, RAM, OS, build configuration, source 수, DNA 수, pair 수를 함께 남긴다.

## 테스트

```powershell
uv run python -m unittest discover -s benchmark/tests -v
```

테스트는 POJ adapter의 split-qualified ID, deterministic balanced manifest, ROC-AUC, 동점에
안전한 PR-AUC 계산, JPlag 결과의 manifest 매핑을 검증한다.

현재 저장소에서 실행한 결과는 [RESULTS.md](RESULTS.md)에 기록한다.
