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
<cpptr-cli> compare <dna_dir> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang>
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