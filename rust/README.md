# cppTR Rust port

C++ cppTR을 Rust로 옮기는 작업 공간이다. 목표는 Linux, macOS, Windows 단일 바이너리 배포,
DNA 생성과 pair 비교 병렬화, 그리고 tree-sitter 기반 다중 언어(Python, Haskell, Erlang 등)
지원이다.

## 구성

```text
crates/cpptr-core       언어 무관 core: DNA 로딩, token 빈도, SC/FV Smith-Waterman 정렬, 병렬 비교
crates/cpptr-frontend   언어별 front end. Frontend trait, C/C++ 스캐너(cfamily), 설정과 키워드 표
crates/cpptr-cli        C++ cpptr-cli와 인자, DNA 형식, CSV 형식이 같은 CLI
```

새 언어는 `cpptr-frontend`에 `Frontend` trait 구현을 추가해 지원한다. 비교 core는 DNA만
다루므로 바꿀 필요가 없다.

## 빌드와 테스트

```sh
cargo build --release          # target/release/cpptr-cli
cargo test                     # 단위 테스트와 C++ 출력 golden 테스트
```

## 배포

`.github/workflows/rust.yml`이 push와 PR마다 Linux, Windows, macOS에서 fmt, clippy, test를
실행한다. `v*` 태그를 push하면 다음 바이너리를 GitHub Release에 올린다.

| target | 비고 |
|---|---|
| `x86_64-unknown-linux-musl`, `aarch64-unknown-linux-musl` | 정적 링크, glibc 버전과 무관 |
| `x86_64-pc-windows-msvc` | CRT 정적 링크(`.cargo/config.toml`), VC++ 재배포 패키지 불필요 |
| `aarch64-apple-darwin`, `x86_64-apple-darwin` | Apple Silicon, Intel Mac |

키워드 표와 기본 설정이 내장되어 있어 압축을 풀면 `cpptr-cli` 하나로 동작한다.

## 현재 지원 범위

| 명령 | 상태 |
|---|---|
| `generate`, `generate-batch` | 완료. DNA 파일이 C++과 바이트 단위로 동일. batch는 파일 단위 병렬 |
| `similarity` (기본 명령) | 완료. 임시 디렉터리 없이 메모리에서 DNA 생성 후 비교 |
| `compare` | 완료. C++과 CSV, `--lines`, 통계 출력이 동일 |
| `compare-manifest` | 완료. 벤치마크 스크립트에서 그대로 사용 가능 |

C++ 도구와 다른 점:

- 기본 설정과 키워드 표가 바이너리에 내장되어 있다. `-` 또는 설정 생략 시 resources 디렉터리를
  찾지 않는다. `cconfig.ini` 형식의 설정 파일은 그대로 쓸 수 있다.
- `--jobs <n>`으로 작업 스레드 수를 정한다. 기본값은 논리 CPU 전체다.
- CSV 줄바꿈은 모든 OS에서 LF다(C++ Windows 빌드는 CRLF). DNA 파일은 두 구현 모두 LF다.
- `generate-batch`가 실패 파일이 있을 때 마지막에 `error: N source file(s) failed`를 한 줄 더
  출력한다.

## C++과의 동등성 검증 (2026-09-23)

기준은 C++ cpptr-cli 0.2.0(MSVC Debug)이다.

- DNA 생성: POJ-104 validation 1,256개와 test 6,553개 source에 `generate-batch`를 실행해
  C++ 출력과 7,809개 파일 모두 바이트 단위로 같았다. CR 전용 줄바꿈, CRLF와 UTF-8 한글,
  CP949, 빈 파일, 주석만 있는 파일, 파일 앞 블록 주석, 클래스와 생성자 초기화 목록, 상호
  재귀, 깊은 호출 체인, 괄호 불균형 등 11개 edge case도 같았다(`tests/fixtures/scanner`).
- 비교: Rust가 만든 DNA로 실행한 POJ validation/test SC/FV `compare-manifest` 결과가
  `benchmark/work/poj104/*/results-{sc,fv}/Benchmark-Result.csv`와 모든 행이 같았다.
  validation DNA 60개 전체 pair의 `compare --lines` 결과와 통계 출력도 같았다.
- `similarity`: 기본 출력, `--csv`, `--mode SC --params`, `--config` 조합 20건이 같았다.

`tests/golden.rs`는 DNA 12개 fixture 비교와 edge case DNA 생성을 반복 검증한다.

## 실행시간 (16 논리 CPU)

| 작업 | C++ MSVC Debug | Rust release |
|---|---:|---:|
| `generate-batch` POJ validation 1,256개 | 7.8 s | 0.60 s |
| `generate-batch` POJ test 6,553개 | 31.8 s | 3.68 s |
| `compare-manifest` POJ test FV 4,800쌍 | 18.8 s | 0.52 s (`--jobs 1`: 0.80 s) |

대부분의 차이는 Debug와 Release 빌드 차이이며 C++ Release와는 아직 비교하지 않았다.
`compare-manifest`는 이 규모에서 DNA 6,553개 로딩이 시간의 큰 부분을 차지해 병렬화 이득이
작다. pair 수가 많은 전체 비교(`compare`)에서 이득이 커진다.

## 다음 단계

1. GitHub에 push해 workflow 실제 실행 확인(아직 로컬 Windows 빌드만 확인)
2. tree-sitter 기반 front end 구조와 언어 간 공통 DNA 어휘 설계
3. Python, Haskell, Erlang front end 추가
