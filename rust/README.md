# cppTR Rust port

C++ cppTR을 Rust로 옮기는 작업 공간이다. 목표는 Linux, macOS, Windows 단일 바이너리 배포,
pair 비교 병렬화, 그리고 tree-sitter 기반 다중 언어(Python, Haskell, Erlang 등) 지원이다.

## 구성

```text
crates/cpptr-core   언어 무관 core: DNA 로딩, token 빈도, SC/FV Smith-Waterman 정렬, 병렬 비교
crates/cpptr-cli    C++ cpptr-cli와 인자 및 CSV 형식이 같은 CLI
```

## 빌드와 테스트

```sh
cargo build --release          # target/release/cpptr-cli
cargo test                     # 단위 테스트와 C++ 출력 golden 테스트
```

## 현재 지원 범위

| 명령 | 상태 |
|---|---|
| `compare` | 완료. C++과 CSV, `--lines`, 통계 출력이 동일 |
| `compare-manifest` | 완료. 벤치마크 스크립트에서 그대로 사용 가능 |
| `generate`, `generate-batch`, `similarity` | 미이식. C/C++ 스캐너 이식 후 제공 |

추가 옵션 `--jobs <n>`으로 작업 스레드 수를 정한다. 기본값은 논리 CPU 전체다.
CSV 줄바꿈은 모든 OS에서 LF다(C++ Windows 빌드는 CRLF).

## C++과의 동등성 검증 (2026-09-23)

POJ-104 validation 680쌍과 test 4,800쌍에 SC와 FV로 `compare-manifest`를 실행해
`benchmark/work/poj104/*/results-{sc,fv}/Benchmark-Result.csv`와 비교했고, 4개 조합 모두
모든 행이 일치했다. validation DNA 60개 전체 pair(1,770쌍)의 `compare --lines` 결과와 통계
출력도 C++ 0.2.0과 같았다. `tests/golden.rs`는 같은 비교를 DNA 12개 fixture로 반복한다.

POJ test FV 4,800쌍 실행시간(16 논리 CPU):

| 구현 | 시간 |
|---|---:|
| C++ MSVC Debug | 18.8 s |
| Rust release, `--jobs 1` | 0.80 s |
| Rust release, 전체 스레드 | 0.52 s |

대부분의 차이는 Debug와 Release 빌드 차이이며 C++ Release와는 아직 비교하지 않았다. 이
규모에서는 DNA 6,553개 로딩이 시간의 큰 부분을 차지해 병렬화 이득이 작다. pair 수가 많은
전체 비교(`compare`)에서 이득이 커진다.

## 다음 단계

1. C/C++ 스캐너(`src/lib/dna_pipeline.cpp`)를 DNA 출력이 동일하도록 이식하고 golden 테스트 추가
2. GitHub Actions로 Windows, Linux(musl 정적), macOS(arm64, x86_64) 릴리스 빌드
3. tree-sitter 기반 `Frontend` trait과 언어별 DNA 매핑 테이블 설계
4. Python, Haskell, Erlang frontend 추가
