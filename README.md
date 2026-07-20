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

Project CodeNet C++1000의 50만 개 source는 DuckDB 하나로 통합한 후 고정 seed로 평가 쌍을 추출할 수 있습니다. 다운로드한 corpus가 `benchmark/Project_CodeNet_Cpp_1000`에 있다고 가정합니다.

```powershell
uv run python benchmark/codenet_benchmark.py bundle `
	benchmark/Project_CodeNet_Cpp_1000 `
	benchmark/Project_CodeNet_Cpp_1000.duckdb

uv run python benchmark/codenet_benchmark.py sample `
	benchmark/Project_CodeNet_Cpp_1000.duckdb `
	--positive-per-problem 10 `
	--negative-per-problem 10 `
	--seed 20260720

uv run python benchmark/codenet_benchmark.py materialize `
	benchmark/Project_CodeNet_Cpp_1000.duckdb `
	benchmark/materialized/cpp1000
```

기본 표본은 1,000개 문제에서 같은 문제 제출물 10쌍과 source 크기가 비슷한 다른 문제 제출물 10쌍을 각각 선택해 총 20,000쌍을 만듭니다. 같은 문제 쌍은 실제 표절 정답이 아니라 semantic similarity의 proxy입니다.

```powershell
<cpptr-cli> generate-batch `
	src/resources/config/cconfig.ini `
	benchmark/materialized/cpp1000/sources `
	benchmark/materialized/cpp1000/dna

<cpptr-cli> compare-manifest `
	benchmark/materialized/cpp1000/dna `
	benchmark/materialized/cpp1000/pairs.csv `
	1 1 1 1 FV `
	benchmark/materialized/cpp1000/results `
	CPP
```

결과 CSV에서 최적 F1 임계치와 지정 임계치별 confusion matrix를 계산합니다.

```powershell
uv run python benchmark/codenet_benchmark.py evaluate `
	benchmark/materialized/cpp1000/results/Benchmark-Result.csv `
	--output benchmark/materialized/cpp1000/results/metrics.json
```

표본을 다시 만들면 기존 `benchmark_pairs`는 교체됩니다. 동일한 DB, 옵션과 seed는 동일한 pair manifest를 생성합니다. `status` 명령으로 DB와 pair 개수를 확인할 수 있습니다.

```powershell
uv run python benchmark/codenet_benchmark.py status `
	benchmark/Project_CodeNet_Cpp_1000.duckdb
```

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