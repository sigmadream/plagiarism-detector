# cppTR

> cppTR은 C/C++ 소스에서 프로그램 DNA를 생성하고, DNA 간 유사도를 비교해 표절 검토 자료를 만드는 C++23 기반 도구입니다.

## 주요 기능

- Adaptive Local Alignment 기반 비교
- C/C++ 소스에서 `.DNA`와 원본 스냅샷 생성
- DNA 디렉터리 비교 및 CSV 결과 생성

## 빌드

```bash
cmake -S src -B build/cpptr
cmake --build build/cpptr --config Debug
```

## 테스트

```bash
ctest --test-dir build/cpptr -C Debug --output-on-failure
```

## CLI

아래 예시의 `<cpptr-cli>`는 위 빌드 결과에 맞는 실행 파일 경로로 바꿉니다.

```text
<cpptr-cli> --help
<cpptr-cli> generate <config.ini> <src_file> <dna_dir>
<cpptr-cli> generate-batch <config.ini> <src_dir> <dna_dir>
<cpptr-cli> compare <dna_dir> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang>
<cpptr-cli> compare-manifest <dna_dir> <pairs.csv> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang>
```

### DNA 생성

```text
<cpptr-cli> generate <config.ini> <src_file> <dna_dir>
```

- `config.ini`: 언어와 키워드 테이블 경로를 지정하는 설정 파일
- `src_file`: 분석할 C/C++ 소스 파일
- `dna_dir`: 결과 디렉터리

```text
<dna_dir>/<소스 파일명>.DNA
<dna_dir>/<소스 파일명>.DNA.src
```

#### DNA 토큰 추출 규칙

`.DNA`의 각 행은 `토큰 이름`, `줄`, `열`입니다. 스캐너는 문자열, 문자 리터럴, 주석, `#`으로 시작하는 전처리 줄을 제거한 뒤 키워드 테이블(`src/resources/keywords/*.tbl`)의 첫 열에 있는 토큰만 기록합니다. 테이블에서 이름을 지우면 해당 토큰은 DNA에서 빠집니다.

- 타입, 제어문, 클래스 관련 키워드 44종을 대응하는 토큰으로 기록합니다. `nullptr`은 `NULL`로 기록합니다.
- 산술, 비교, 논리, 비트, 대입, 증감 연산자 32종과 `{`, `}`를 최장 일치로 기록합니다. `<<`는 `SHL`, `<`는 `LT`처럼 스트림 연산자와 템플릿 괄호도 연산자 토큰으로 취급합니다.
- 함수 호출은 정적 추적합니다. 같은 파일에 정의된 함수를 호출하면 `FUNC_CALL`, 호출 대상의 매개변수와 본문 토큰, `FUNC_END` 순서로 호출 지점에 인라인하며, 정의를 찾을 수 없는 호출은 `UNTRACKABLE_FUNC_CALL` 하나로 기록합니다.
- 어디에서도 호출되지 않는 함수와 재귀 섬처럼 도달할 수 없는 함수는 정의 위치에 그대로 기록합니다. 호출되는 함수는 호출 지점에서만 나타납니다.
- 재귀 호출과 인라인 깊이 8 초과, 누적 토큰 25만 개 초과 시에는 본문 없이 `FUNC_CALL`, `FUNC_END`만 기록합니다.
- 함수 프로토타입과 `vector<int> v(n);` 같은 생성자 초기화 선언은 기록하지 않습니다. 생성자 초기화 목록도 건너뜁니다.
- 줄 구분은 LF, CRLF, CR을 모두 인식합니다. 토큰이 하나도 없는 파일은 오류로 처리합니다.

### 비교

```text
<cpptr-cli> compare <dna_dir> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang>
```

- `alpha`, `beta`: match와 mismatch 계수
- `gamma`, `delta`: insertion과 deletion gap 계수
- `mode`: `SC` 또는 `FV`
- `lang`: `C` 또는 `CPP`

```text
<output_dir>/Plag-Detection-Result.csv
```

### 벤치마크 비교

`generate-batch`는 디렉터리 아래 C/C++ 파일을 한 프로세스에서 재귀적으로 처리합니다. materialized source가 이미 보존되어 있으므로 `.DNA.src` 스냅샷은 남기지 않습니다. 같은 파일명이 여러 디렉터리에 있으면 DNA 출력이 충돌하므로 오류로 종료합니다.

`compare-manifest`는 전체 파일 조합 대신 manifest에 기록된 쌍만 비교합니다. manifest는 다음 헤더를 사용합니다.

```csv
left_dna,right_dna,label,pair_id,kind,split
```

결과는 `<output_dir>/Benchmark-Result.csv`에 기록되며 정답 label과 `pair_id`를 포함합니다.

## Project CodeNet 벤치마크

이 벤치마크는 Project CodeNet C++1000의 제출물을 사용해 같은 문제를 푼 코드와 서로 다른 문제를 푼 코드의 유사도 점수 분포를 비교합니다. 같은 문제 쌍은 실제 표절 정답이 아니라 semantic similarity의 proxy이므로 결과를 표절 탐지 정확도로 해석하면 안 됩니다.

### 사전 준비

- `uv`가 설치되어 있어야 합니다.
- Project CodeNet C++1000 corpus를 `benchmark/Project_CodeNet_Cpp_1000`에 배치합니다.
- corpus는 `p00000/*.cpp`처럼 문제별 디렉터리 아래에 C++ 제출물이 있어야 합니다.
- cppTR을 Debug 구성으로 빌드합니다.

```powershell
cmake -S src -B build/cpptr
cmake --build build/cpptr --config Debug
$cpptr = "build/cpptr/apps/cpptr-cli/Debug/cpptr-cli.exe"
```

### 1. Corpus 통합

50만 개의 작은 source 파일을 단일 DuckDB 파일로 통합합니다. `--replace`는 기존 DB를 삭제하고 다시 생성하므로, 최초 생성 이후에는 필요한 경우에만 사용합니다.

```powershell
uv run python benchmark/codenet_benchmark.py bundle `
	benchmark/Project_CodeNet_Cpp_1000 `
	benchmark/Project_CodeNet_Cpp_1000.duckdb `
	--replace
```

현재 C++1000 corpus에서는 1,000개 문제와 500,000개 제출물이 약 765 MiB의 DuckDB 파일로 통합됩니다. 원본 corpus는 자동으로 삭제되지 않습니다.

### 2. 평가 Pair 추출

각 문제에서 같은 문제 제출물 10쌍을 positive로 선택하고, source 크기가 비슷한 다른 문제 제출물 10쌍을 negative로 선택합니다. 고정 seed를 사용하므로 동일한 DB와 옵션에서는 같은 pair가 생성됩니다.

```powershell
uv run python benchmark/codenet_benchmark.py sample `
	benchmark/Project_CodeNet_Cpp_1000.duckdb `
	--positive-per-problem 10 `
	--negative-per-problem 10 `
	--seed 20260720
```

이 명령은 DB의 기존 `benchmark_pairs`를 교체합니다. 기본 설정의 결과는 다음과 같습니다.

```text
전체 pair: 20,000
positive:   10,000
negative:   10,000
```

### 3. 작업 데이터 추출

선택된 pair에서 사용하는 source만 작업 디렉터리에 추출하고 `pairs.csv`를 생성합니다. `--replace`는 기존 작업 디렉터리와 이전 결과를 삭제합니다.

```powershell
uv run python benchmark/codenet_benchmark.py materialize `
	benchmark/Project_CodeNet_Cpp_1000.duckdb `
	benchmark/materialized/cpp1000 `
	--replace
```

현재 seed에서는 20,000개 pair가 참조하는 고유 source 37,712개가 추출됩니다.

```text
benchmark/materialized/cpp1000/
  sources/       선택된 C++ source
  pairs.csv      label이 포함된 비교 pair manifest
  summary.json   source와 pair 개수
```

### 4. DNA 일괄 생성

선택된 source의 DNA를 한 프로세스에서 생성합니다. materialized source가 이미 보존되어 있으므로 batch 명령은 중복되는 `.DNA.src` 파일을 남기지 않습니다.

```powershell
& $cpptr generate-batch `
	src/resources/config/cconfig.ini `
	benchmark/materialized/cpp1000/sources `
	benchmark/materialized/cpp1000/dna
```

정상 완료 시 생성 수와 실패 수가 출력됩니다. 실패가 한 건이라도 있으면 비교를 진행하기 전에 원인을 해결해야 합니다.

### 5. Manifest 비교

전체 조합을 만들지 않고 `pairs.csv`에 기록된 20,000개 pair만 FV 모드로 비교합니다.

```powershell
& $cpptr compare-manifest `
	benchmark/materialized/cpp1000/dna `
	benchmark/materialized/cpp1000/pairs.csv `
	1 1 1 1 FV `
	benchmark/materialized/cpp1000/results `
	CPP
```

결과는 `benchmark/materialized/cpp1000/results/Benchmark-Result.csv`에 기록됩니다. 각 행에는 pair ID, 정답 label, 유사도 점수와 정렬 구간이 포함됩니다.

### 6. 평가 지표 계산

결과 CSV에서 최적 F1 임계치와 기본 임계치 50, 70, 90의 TP, FP, FN, TN, precision, recall, F1을 계산합니다.

```powershell
uv run python benchmark/codenet_benchmark.py evaluate `
	benchmark/materialized/cpp1000/results/Benchmark-Result.csv `
	--output benchmark/materialized/cpp1000/results/metrics.json
```

추가 임계치를 평가하려면 `--threshold`를 반복해서 전달합니다.

```powershell
uv run python benchmark/codenet_benchmark.py evaluate `
	benchmark/materialized/cpp1000/results/Benchmark-Result.csv `
	--threshold 20 `
	--threshold 30 `
	--threshold 40
```

### 결과 확인

DB와 추출된 pair 개수는 다음 명령으로 확인합니다.

```powershell
uv run python benchmark/codenet_benchmark.py status `
	benchmark/Project_CodeNet_Cpp_1000.duckdb
```

현재 `alpha=1`, `beta=1`, `gamma=1`, `delta=1`, `FV` 설정의 기준 결과는 다음과 같습니다.

| 임계치 | Precision | Recall | F1 |
| ---: | ---: | ---: | ---: |
| 최적 `21.9347` | 0.5735 | 0.8669 | 0.6903 |
| `50` | 0.7499 | 0.3349 | 0.4630 |
| `70` | 0.8293 | 0.1117 | 0.1969 |
| `90` | 0.8622 | 0.0194 | 0.0379 |

최적 임계치를 같은 20,000개 pair에서 선택하고 평가했기 때문에 이 값은 독립적인 일반화 성능이 아니라 현재 표본의 기준값입니다. 최종 성능을 보고할 때는 train/validation/test split을 분리하거나 별도의 검증 corpus에서 임계치를 평가해야 합니다.

`SC`와 `FV`를 비교하거나 계수를 변경할 때는 5단계의 명령만 다른 설정으로 다시 실행하고 결과 디렉터리를 분리합니다. pair와 seed를 동일하게 유지해야 설정 간 비교가 가능합니다.

## 빠른 실행 예시

프로젝트에 포함된 두 C++ fixture로 DNA를 생성한 후 비교합니다. 먼저 `build/example/dna`와 `build/example/result` 디렉터리를 생성합니다.

```text
<cpptr-cli> generate src/resources/config/cconfig.ini src/tests/fixtures/source_corpus/cpp/201213119.cpp build/example/dna
<cpptr-cli> generate src/resources/config/cconfig.ini src/tests/fixtures/source_corpus/cpp/201224507.cpp build/example/dna
<cpptr-cli> compare build/example/dna 1 1 1 1 FV build/example/result CPP
```

## 설정과 리소스

```text
src/resources/config/cconfig.ini
src/resources/keywords/c_keyword.tbl
src/resources/keywords/cpp_keyword.tbl
```

명시적으로 전달한 설정 파일을 가장 먼저 사용합니다. 설정 경로가 비어 있으면 source, build, install 리소스 루트 순서로 기본 설정을 탐색합니다. 설정에 기록된 상대 키워드 테이블 경로는 해당 설정 파일을 기준으로 해석되며, 파일이 없으면 오류로 처리합니다.

## Ref

- 관련 연구 데이터셋: [PAN 2014 Source Code Re-use](https://pan.webis.de/fire14/pan14-web/)
- Functional Terminal (X) User interface:  [FTXUI](https://github.com/ArthurSonzogni/FTXUI)
- BigCloneEval: Evaluating Clone Detection Tools with BigCloneBench: [BigCloneEval](https://github.com/jeffsvajlenko/BigCloneEval)