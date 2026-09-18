# POJ-104 results

## 2026-09-18: 확장 스캐너와 JPlag baseline

실행일: 2026-09-18. cppTR은 키워드 테이블 전체(89종)와 함수 호출 정적 추적을 지원하는
스캐너로 실행했다. JPlag 6.3.0은 `-l cpp` 기본 설정으로 같은 source와 manifest에
실행했으며(`benchmark/jplag.py`), average similarity x 100을 score로 사용했다.

### 환경

- OS: Windows 11
- cppTR build: MSVC Debug
- cppTR options: `1 1 1 1`, language `CPP`
- JPlag 6.3.0, Temurin JDK 25
- random seed: `20260720`
- positive/negative ratio: `1:1`

### Validation pilot (17 문제, 1,256 source, 680 pair)

| system | ROC-AUC | PR-AUC | pilot threshold | precision | recall | F1 |
|---|---:|---:|---:|---:|---:|---:|
| SC | 0.7963 | 0.8178 | 9.375 | 0.6869 | 0.8000 | 0.7391 |
| FV | 0.8323 | 0.8543 | 10.472 | 0.7092 | 0.8176 | 0.7596 |
| JPlag | 0.5782 | 0.5797 | (0.0) | - | - | - |

JPlag는 validation에서 best F1 threshold가 0.0(모두 positive로 판정)으로 퇴화하여 pilot
threshold를 정할 수 없었다. JPlag가 비교하지 않은 pair 4개는 score 0으로 처리했다.
SC와 FV의 pilot threshold는 test 결과를 보기 전에 고정했다.

### Test (24 문제, 6,553 source, 4,800 pair)

DNA generation failure: 0. JPlag가 비교하지 않은 pair 39개(토큰 수 부족)는 score 0으로
처리했다.

| system | ROC-AUC | PR-AUC | fixed threshold | precision | recall | F1 |
|---|---:|---:|---:|---:|---:|---:|
| SC | 0.8003 | 0.8220 | 9.375 | 0.6557 | 0.8292 | 0.7323 |
| FV | 0.8336 | 0.8527 | 10.472 | 0.6866 | 0.8400 | 0.7556 |
| JPlag | 0.5683 | 0.5701 | 50 | 0.9888 | 0.0367 | 0.0707 |

threshold 50 비교:

| system | precision@50 | recall@50 | F1@50 |
|---|---:|---:|---:|
| SC | 0.9885 | 0.0358 | 0.0692 |
| FV | 0.9945 | 0.0758 | 0.1409 |
| JPlag | 0.9888 | 0.0367 | 0.0707 |

구 스캐너 대비 test ROC-AUC는 SC 0.7732에서 0.8003으로, FV 0.7996에서 0.8336으로 올랐고
고정 threshold F1은 SC 0.7042에서 0.7323으로, FV 0.7215에서 0.7556으로 올랐다.

JPlag의 ROC-AUC 0.57은 무작위에 가깝다. JPlag는 복사된 코드 조각을 찾는 도구이며 같은
문제를 독립적으로 푼 코드에는 낮은 점수를 주므로, 이 결과는 JPlag의 결함이 아니라 이
benchmark가 표절 탐지가 아닌 semantic similarity를 측정한다는 사실을 보여 준다.

### 문제 단위 bootstrap 95% CI (test)

BOOTSTRAP_PLACEHOLDER

### 실행시간 (MSVC Debug)

| 단계 | validation | test |
|---|---:|---:|
| cppTR generate-batch | 11 s (1,256 source) | 59 s (6,553 source) |
| cppTR compare-manifest SC | 2 s | 5 s |
| cppTR compare-manifest FV | 4 s | 17 s |
| JPlag (chunk 150) | 91 s | 665 s |

Debug 빌드의 단일 실행값이며 반복 측정과 peak memory는 기록하지 않았다.

## 2026-09-18: 저장소에서 제거된 보조 실험 요약

같은 날 POJ-104 seed로 만든 Type 1~3 mutation corpus와 실제 과제 제출물 실험도 수행했으나,
저장소를 POJ-104 전용으로 정리하면서 해당 코드와 데이터는 제거했다. 재현 자료가 없으므로
아래 수치는 참고용이다.

- mutation corpus(seed 160개, 40 문제, 변형 1,432개, negative 480쌍, clang++ 문법 검사
  통과): threshold 50에서 FV의 recall 0.975, SC 0.927, JPlag 0.817이었고 세 도구 모두 false
  positive는 없었다. Type 3 강한 변형(dead code, 문장과 함수 순서 변경, for/while 치환,
  if/else 교환, 복합 대입 전개, 함수 추출)에서 recall은 FV 0.79, SC 0.43, JPlag 0.09였다.
  cppTR의 DNA는 식별자와 literal을 담지 않으므로 Type 1, 2에서 recall 1.0은 설계상 당연한
  결과다.
- 실제 C/C++ 과제 45개(제출물 1,438개, 23,758쌍): 두 프로그램 모두 DNA 토큰 40개 이상인
  쌍에서 점수 층별로 뽑은 48쌍을 두 LLM 검토자가 blind로 판정한 결과 합의 clone 1쌍, 합의
  non-clone 40쌍, 불일치 7쌍, kappa 0.48이었다. 두 도구 모두 60점 이상인 8쌍 중 합의 clone은
  없었고, 높은 점수의 주원인은 강의 자료 공유(gcd 함수, 스택 클래스 골격)였다. 토큰 20개
  미만의 첫 과제는 어떤 도구로도 구분할 수 없었다.

## 2026-07-20: 구 스캐너 baseline (참고용)

아래 값은 토큰 10종만 추출하던 구 스캐너의 결과이며 현재 기준값이 아니다. 환경은 위와
같다.

### Validation pilot (680 pair)

| mode | ROC-AUC | PR-AUC | pilot threshold | precision | recall | F1 |
|---|---:|---:|---:|---:|---:|---:|
| SC | 0.744161 | 0.766782 | 13.5593 | 0.587426 | 0.879412 | 0.704358 |
| FV | 0.773296 | 0.799908 | 20.2005 | 0.618844 | 0.850000 | 0.716233 |

### Test (4,800 pair)

| mode | ROC-AUC | PR-AUC | fixed threshold | precision | recall | F1 |
|---|---:|---:|---:|---:|---:|---:|
| SC | 0.773234 | 0.775248 | 13.5593 | 0.563910 | 0.937500 | 0.704225 |
| FV | 0.799635 | 0.810255 | 20.2005 | 0.596999 | 0.911667 | 0.721517 |

| mode | precision@50 | recall@50 | F1@50 |
|---|---:|---:|---:|
| SC | 0.911330 | 0.154167 | 0.263721 |
| FV | 0.896719 | 0.307500 | 0.457958 |

## 해석 제한

같은 문제의 제출물은 실제 표절 pair가 아니다. 이 결과는 문제 ID를 proxy label로 사용한
C++ semantic similarity 성능이며, 표절 탐지 precision/recall로 인용하면 안 된다.
`best_f1_exploratory`는 test label로 선택되는 값이므로 최종 표에 사용하지 않았다.
