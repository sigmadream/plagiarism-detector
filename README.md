# cppTR

> cppTR은 C/C++ 소스에서 프로그램 DNA를 생성하고, DNA 간 유사도를 비교해 표절 검토 자료를 만드는 C++23 기반 도구입니다.

- Adaptive Local Alignment 기반 비교 (SC, FV 두 모드)
- C/C++ 소스에서 키워드 테이블 89종 토큰과 함수 호출 정적 추적을 반영한 `.DNA` 생성
- DNA 디렉터리 전체 조합 비교와 manifest 기반 pair 비교, CSV 결과 생성
- POJ-104 벤치마크와 JPlag 비교 파이프라인 (`benchmark/`)

## 빌드

```bash
cmake -S src -B build/cpptr
cmake --build build/cpptr --config Debug
cmake --build build/cpptr --config Release
```

Windows에서는 MSVC(Visual Studio 2022 이상), 그 외 환경에서는 C++23을 지원하는 clang 또는 gcc가 필요합니다. googletest는 CMake FetchContent로 내려받습니다.

## 테스트

```bash
ctest --test-dir build/cpptr -C Debug --output-on-failure
uv run python -m unittest discover -s benchmark/tests -v
```

## CLI

아래 예시의 `<cpptr-cli>`는 빌드 결과의 실행 파일 경로로 바꿉니다. Windows Debug 빌드에서는 `build/cpptr/apps/cpptr-cli/Debug/cpptr-cli.exe`입니다.

### 기본 동작: 두 파일의 유사도

명령 없이 소스 파일 두 개를 주면 배포된 기본 설정으로 DNA를 만들어 바로 비교합니다. 임시 디렉터리에서 처리하므로 별도 산출물이 남지 않습니다.

```text
<cpptr-cli> a.cpp b.cpp
A: a.cpp
B: b.cpp
similarity: 80.896 (FV, CPP)
aligned region: A lines 3-15, B lines 3-13
```

같은 동작을 `similarity` 명령으로 부를 수 있고 다음 옵션을 받습니다.

```text
<cpptr-cli> similarity <src_file1> <src_file2> [--config <ini>] [--mode SC|FV] [--lang C|CPP]
                       [--params <alpha> <beta> <gamma> <delta>] [--csv]
```

- 기본값은 기본 설정, FV 모드, 계수 `1 1 1 1`이며 언어는 두 파일이 모두 `.c` 또는 `.h`면 C, 아니면 CPP
- `--csv`는 사람이 읽는 형식 대신 점수, 토큰 구간, 소스 줄 범위를 한 행의 CSV로 출력
- 두 파일만 비교하므로 적응형 점수의 토큰 빈도는 그 두 파일에서 계산
- 여러 제출물을 한꺼번에 검토할 때는 `generate-batch`와 `compare`를 사용해 과제 전체의 빈도를 반영하는 편이 정확
- 파일 이름이 같아도 비교 가능

### 명령 목록

```text
<cpptr-cli> --help
<cpptr-cli> --version
<cpptr-cli> similarity <src_file1> <src_file2> [options]
<cpptr-cli> generate <config.ini> <src_file> <dna_dir>
<cpptr-cli> generate-batch <config.ini> <src_dir> <dna_dir>
<cpptr-cli> compare <dna_dir> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang> [--lines]
<cpptr-cli> compare-manifest <dna_dir> <pairs.csv> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang> [--lines]
```

### DNA 생성

```text
<cpptr-cli> generate <config.ini> <src_file> <dna_dir>
```

- `config.ini`: 언어와 키워드 테이블 경로를 지정하는 설정 파일. `-`를 주면 실행 파일과 함께 배포된 기본 설정(`share/cpptr/resources/config/cconfig.ini`)을 사용합니다.
- `src_file`: 분석할 C/C++ 소스 파일
- `dna_dir`: 결과 디렉터리

```text
<dna_dir>/<소스 파일명>.DNA
<dna_dir>/<소스 파일명>.DNA.src
```

`generate-batch`는 디렉터리 아래 C/C++ 파일을 한 프로세스에서 재귀적으로 처리하고 `.DNA.src` 스냅샷은 남기지 않습니다. 같은 파일명이 여러 디렉터리에 있으면 DNA 출력이 충돌하므로 오류로 종료합니다.

#### DNA 토큰 추출 규칙

`.DNA`의 각 행은 `토큰 이름`, `줄`, `열`입니다. 줄은 1부터 시작하는 소스 줄 번호, 열은 0부터 시작하는 문자 위치입니다. 스캐너는 문자열, 문자 리터럴, 주석, `#`으로 시작하는 전처리 줄을 제거한 뒤 키워드 테이블(`src/resources/keywords/*.tbl`)의 첫 열에 있는 토큰만 기록합니다. 테이블에서 이름을 지우면 해당 토큰은 DNA에서 빠집니다.

- 타입, 제어문, 클래스 관련 키워드 44종을 대응하는 토큰으로 기록, `nullptr`은 `NULL`로 기록
- 산술, 비교, 논리, 비트, 대입, 증감 연산자 32종과 `{`, `}`를 최장 일치로 기록, `<<`는 `SHL`, `<`는 `LT`처럼 스트림 연산자와 템플릿 괄호도 연산자 토큰으로 취급
- 함수 호출은 정적 추적, 같은 파일에 정의된 함수를 호출하면 `FUNC_CALL`, 호출 대상의 매개변수와 본문 토큰, `FUNC_END` 순서로 호출 지점에 인라인, 정의를 찾을 수 없는 호출은 `UNTRACKABLE_FUNC_CALL` 하나로 기록
- 어디에서도 호출되지 않는 함수와 재귀 섬처럼 도달할 수 없는 함수는 정의 위치에 그대로 기록, 호출되는 함수는 호출 지점에서만 나타남
- 재귀 호출과 인라인 깊이 8 초과, 누적 토큰 25만 개 초과 시에는 본문 없이 `FUNC_CALL`, `FUNC_END`만 기록
- 함수 프로토타입과 `vector<int> v(n);` 같은 생성자 초기화 선언은 기록하지 않음, 생성자 초기화 목록도 건너뜀
- 줄 구분은 LF, CRLF, CR을 모두 인식, 토큰이 하나도 없는 파일은 오류 처리

식별자와 literal은 DNA에 들어가지 않으므로 이름 변경과 상수 변경(Type 2 변형)은 DNA를 바꾸지 않음
반대로 토큰이 20개 미만인 아주 짧은 프로그램은 서로 구분되지 않으므로 비교 대상에서 제외

### 비교

```text
<cpptr-cli> compare <dna_dir> <alpha> <beta> <gamma> <delta> <mode> <output_dir> <lang>
```

- `alpha`, `beta`: match와 mismatch 계수
- `gamma`, `delta`: insertion과 deletion gap 계수
- `mode`: `SC` 또는 `FV`. 벤치마크에서는 FV가 일관되게 우세했습니다.
- `lang`: `C` 또는 `CPP`

```text
<output_dir>/Plag-Detection-Result.csv
```

각 행에는 두 DNA 파일명, 유사도 점수(0~100), 정렬 구간의 DNA 토큰 인덱스(`Begin_P1`, `End_P1`, `Begin_P2`, `End_P2`)가 기록됩니다. 마지막 인자 뒤에 `--lines`를 붙이면 정렬 구간에 포함된 토큰의 소스 줄 범위가 `Line_Begin_P1`, `Line_End_P1`, `Line_Begin_P2`, `Line_End_P2` 네 열로 추가되어 CSV만으로 검토 위치를 찾을 수 있습니다. 호출된 함수가 인라인된 구간은 그 함수 정의의 줄까지 범위에 포함됩니다.

`compare-manifest`는 전체 파일 조합 대신 manifest에 기록된 쌍만 비교합니다. manifest는 다음 헤더를 사용하고, 결과는 `<output_dir>/Benchmark-Result.csv`에 정답 label과 `pair_id`를 포함해 기록됩니다.

```csv
left_dna,right_dna,label,pair_id,kind,split
```

### 빠른 실행 예시

프로젝트에 포함된 두 C++ fixture로 DNA를 생성한 후 비교합니다.

```text
<cpptr-cli> generate src/resources/config/cconfig.ini src/tests/fixtures/source_corpus/cpp/201213119.cpp build/example/dna
<cpptr-cli> generate src/resources/config/cconfig.ini src/tests/fixtures/source_corpus/cpp/201224507.cpp build/example/dna
<cpptr-cli> compare build/example/dna 1 1 1 1 FV build/example/result CPP
```

## 벤치마크

`benchmark/`는 POJ-104로 SC와 FV를 평가하고 같은 pair에 JPlag 6.3.0을 실행해 비교합니다. 실행 절차는 [benchmark/README.md](benchmark/README.md), 결과는 [benchmark/RESULTS.md](benchmark/RESULTS.md)에 있습니다.

```text
benchmark/
  benchmark.py    POJ-104 통합(DuckDB), pair 추출, materialize, 지표 계산
  bootstrap.py    문제 단위 paired bootstrap 95% CI
  jplag.py        JPlag 실행과 결과 변환 (Java 25 필요)
  tests/          회귀 테스트
  POJ-104/        Parquet 3개 (Git 제외)
  work/           생성 결과 (Git 제외)
```

Python 의존성은 `pyproject.toml`에 있으며 `uv sync`로 설치합니다.

## 배포

CLI 사용자에게는 실행 파일과 기본 리소스만 담은 압축 파일을 배포합니다. 벤치마크 Python 코드, 코퍼스, JPlag jar는 포함되지 않습니다.

```bash
cmake -S src -B build/release -DCMAKE_BUILD_TYPE=Release -DCPPTR_BUILD_TESTS=OFF -DCPPTR_STATIC_RUNTIME=ON
cmake --build build/release --config Release
cpack --config build/release/CPackConfig.cmake -C Release
```

결과는 `build/release/package/cpptr-<버전>-<os>-<arch>.zip`(Windows) 또는 `.tar.gz`(Linux, macOS)이며 SHA-256 체크섬 파일이 함께 생성됩니다. 압축 안의 구성은 다음과 같고, 어느 위치에 풀어도 실행 파일이 자기 위치를 기준으로 `share/cpptr/resources`를 찾으므로 설정 인자에 `-`만 주면 됩니다.

```text
cpptr-<버전>-<os>-<arch>/
  bin/cpptr-cli
  share/cpptr/resources/config/cconfig.ini
  share/cpptr/resources/keywords/*.tbl
  share/doc/cpptr/README.md
```

- `CPPTR_STATIC_RUNTIME=ON`은 MSVC에서 `/MT`, Linux에서 `-static-libstdc++ -static-libgcc`로 링크해 런타임 설치 없이 실행되게 합니다.
- `CPPTR_BUILD_TESTS=OFF`는 googletest 다운로드와 테스트 빌드를 생략합니다.
- `cmake --install build/release --prefix <경로>`로 같은 구성을 직접 설치할 수도 있습니다. 소스 압축은 `cpack --config build/release/CPackSourceConfig.cmake`로 만듭니다.
- 버전은 `src/CMakeLists.txt`의 `project(... VERSION ...)`에서 관리하며 `cpptr-cli --version`이 같은 값을 출력합니다.

### 사용자 설치

압축을 원하는 위치에 풀고 `bin` 디렉터리를 PATH에 넣거나 전체 경로로 실행합니다. 별도 런타임이나 설정 파일 준비는 필요 없습니다.

```powershell
# Windows
Expand-Archive cpptr-0.2.0-windows-x86_64.zip -DestinationPath C:\Tools
C:\Tools\cpptr-0.2.0-windows-x86_64\bin\cpptr-cli.exe --version
C:\Tools\cpptr-0.2.0-windows-x86_64\bin\cpptr-cli.exe a.cpp b.cpp
```

```bash
# Linux, macOS
tar -xzf cpptr-0.2.0-linux-x86_64.tar.gz -C ~/tools
~/tools/cpptr-0.2.0-linux-x86_64/bin/cpptr-cli --version
~/tools/cpptr-0.2.0-linux-x86_64/bin/cpptr-cli a.cpp b.cpp
```

받은 파일이 손상되지 않았는지는 함께 배포되는 `.sha256` 파일로 확인합니다.

```powershell
# Windows
(Get-FileHash cpptr-0.2.0-windows-x86_64.zip -Algorithm SHA256).Hash
Get-Content cpptr-0.2.0-windows-x86_64.zip.sha256
```

```bash
# Linux, macOS
sha256sum -c cpptr-0.2.0-linux-x86_64.tar.gz.sha256
```

`bin` 디렉터리만 따로 복사하면 기본 설정을 찾지 못하므로 압축 구조를 유지해야 합니다. 실행 파일 옆에 `resources/` 디렉터리를 두는 평면 구조도 인식합니다.

### 릴리스 절차

1. `src/CMakeLists.txt`의 `project(... VERSION ...)`을 올리고 `ctest`와 benchmark 테스트가 통과하는지 확인합니다.
2. 배포 대상 OS마다 위 명령으로 Release 빌드와 `cpack`을 실행합니다. Windows는 MSVC, Linux는 gcc 14 이상 또는 clang, macOS는 Apple clang이 필요하며 CPack이 OS와 아키텍처를 파일명에 붙입니다. 크로스 컴파일은 지원하지 않으므로 각 OS에서 직접 빌드합니다.
3. `git tag v<버전>`을 만들어 푸시하고, GitHub Release에 각 OS의 압축 파일과 `.sha256` 파일, 소스 압축을 첨부합니다. Release 본문에는 변경 요약과 `cpptr-cli --version` 출력값을 적습니다.
4. 벤치마크 결과를 갱신했다면 `benchmark/RESULTS.md`의 실행일과 버전을 함께 기록합니다.

GitHub Actions로 세 OS의 빌드와 Release 첨부를 자동화할 수 있습니다. 태그 푸시를 트리거로 `windows-latest`, `ubuntu-latest`, `macos-latest`에서 위 세 명령을 실행하고 `build/release/package/*`를 업로드하면 됩니다.

### 패키지 관리자

GitHub Release 자산을 그대로 가리키는 manifest만 추가하면 되므로 별도 빌드가 필요 없습니다.

- Windows: scoop bucket 또는 winget manifest에 zip URL과 SHA-256, `bin/cpptr-cli.exe`를 등록합니다.
- macOS, Linux: Homebrew tap의 formula에서 tar.gz URL과 SHA-256을 지정하고 `bin/cpptr-cli`와 `share/cpptr`를 함께 설치합니다.
- vcpkg와 Conan은 라이브러리 배포용이라 CLI 사용자 대상 배포에는 맞지 않습니다.

## 설정과 리소스

```text
src/resources/config/cconfig.ini
src/resources/keywords/c_keyword.tbl
src/resources/keywords/cpp_keyword.tbl
```

명시적으로 전달한 설정 파일을 가장 먼저 사용합니다. 설정 경로가 비어 있거나 `-`이면 실행 파일 기준 `../share/cpptr/resources`와 `./resources`, 그다음 빌드 시점에 기록된 source, build, install 리소스 루트 순서로 기본 설정을 탐색합니다. 설정에 기록된 상대 키워드 테이블 경로는 해당 설정 파일을 기준으로 해석되며, 파일이 없으면 오류로 처리합니다.

## Ref

- 지정훈, 적응적 서열 정렬 기법을 이용한 프로그램 유사도 분석 프레임워크, 부산대학교 박사학위논문, 2010 (`ref/`)
- 김규식 외, 소스코드 유사도 측정 도구의 성능에 관한 비교연구, 한국소프트웨어감정평가학회 논문지 13(1), 2017 (`ref/`)
- 관련 연구 데이터셋: [PAN 2014 Source Code Re-use](https://pan.webis.de/fire14/pan14-web/)
- POJ-104: [CodeXGLUE clone detection](https://huggingface.co/datasets/google/code_x_glue_cc_clone_detection_poj104)
- JPlag: [jplag/JPlag](https://github.com/jplag/JPlag)
- Functional Terminal (X) User interface: [FTXUI](https://github.com/ArthurSonzogni/FTXUI)
- BigCloneEval: Evaluating Clone Detection Tools with BigCloneBench: [BigCloneEval](https://github.com/jeffsvajlenko/BigCloneEval)
