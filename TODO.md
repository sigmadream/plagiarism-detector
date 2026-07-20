# cppTR 벤치마크 및 논문 실험 TODO

## 연구 범위

현재 보유한 Project CodeNet C++1000, POJ-104, 실제 수강 제출물과 cppTR의 `SC`/`FV`
모드를 이용해 다음 세 영역을 평가한다.

1. 의미적 유사성: 같은 문제와 다른 문제 풀이의 점수 분리
2. 구문적 클론: Type 1~3 mutation 탐지 성능
3. 확장성: source 및 pair 증가에 따른 실행시간과 peak memory

CodeNet과 POJ의 문제 ID는 semantic similarity의 proxy label이며 실제 표절 정답이
아니다. 따라서 이 결과를 "표절 탐지 정확도"로 보고하지 않는다.

## 현재 상태

- [x] POJ-104 validation/test pair sampling 및 SC/FV baseline
- [x] POJ validation에서 mode별 threshold 선택 후 test에 고정 적용
- [x] POJ ROC-AUC, PR-AUC, precision, recall, F1 계산
- [x] CodeNet C++1000 DuckDB와 20,000-pair manifest 생성
- [x] CodeNet C++1000 FV baseline
- [ ] CodeNet C++1000 SC baseline

현재 절차와 결과:

- [benchmark/semantic_similarity/README.md](benchmark/semantic_similarity/README.md)
- [benchmark/semantic_similarity/RESULTS.md](benchmark/semantic_similarity/RESULTS.md)

## 공통 원칙

- random seed, cppTR build, 설정, numeric option과 OS를 기록한다.
- SC와 FV는 같은 source, pair manifest 및 실행 환경에서 비교한다.
- threshold는 validation에서 선택하고 test 결과를 보기 전에 고정한다.
- test에서 얻은 최적 threshold는 탐색값으로만 표시한다.
- pair가 source와 문제를 공유하므로 문제 단위 통계 분석을 사용한다.
- 원본 결과 CSV, manifest와 분석 산출물을 보존한다.

## 1. CodeNet C++1000 SC 결과 추가 및 FV 비교

### 작업

- [ ] 기존 20,000-pair manifest와 materialized source 상태 확인
- [ ] 기존 DNA와 같은 manifest를 사용해 `SC` 비교 실행
- [ ] SC/FV의 pair ID, label 및 20,000개 결과 행 일치 확인
- [ ] ROC-AUC, PR-AUC와 threshold 50/70/90 지표 계산
- [ ] score 분포와 문제별 성능 요약 생성
- [ ] SC/FV 비교표와 해석 제한 기록

### 산출물

```text
results-sc/Benchmark-Result.csv
results-sc/metrics.json
results-fv/Benchmark-Result.csv
results-fv/metrics.json
```

### 완료 조건

- 동일한 20,000개 pair에서 SC/FV 결과가 생성된다.
- ROC-AUC, PR-AUC, precision, recall, F1 비교표가 작성된다.
- 같은 문제 pair가 실제 표절 정답이 아님을 명시한다.

## 2. 문제 단위 bootstrap과 95% 신뢰구간 구현

### 작업

- [ ] 각 pair를 원래 `problem_id`와 연결하는 metadata 보존
- [ ] 문제 ID를 cluster로 사용하는 paired bootstrap 구현
- [ ] SC/FV에 동일한 bootstrap sample 적용
- [ ] ROC-AUC, PR-AUC 및 고정 threshold F1의 95% percentile CI 계산
- [ ] SC-FV 지표 차이와 95% CI 계산
- [ ] seed와 반복 횟수를 CLI option으로 제공
- [ ] deterministic fixture test 추가

기본 반복 횟수는 10,000회로 하고 pilot에서는 1,000회로 실행시간을 확인한다.

### 산출물

```text
metrics-bootstrap.json
problem-metrics.csv
```

### 완료 조건

- 같은 seed에서 동일한 CI가 재현된다.
- SC/FV 차이의 CI가 0을 포함하는지 보고할 수 있다.
- pair 행을 독립 관측치처럼 직접 재표본화하지 않는다.

## 3. POJ full retrieval 평가 기능 구현

POJ-104의 공식 과제에 맞춰 balanced pair classification 외에 query별 동일 문제 source
검색 성능을 평가한다.

### 작업

- [ ] POJ test 12,000개 source의 query/candidate label 준비
- [ ] DNA를 한 번만 적재하는 blockwise 비교 구현
- [ ] 약 7,200만 pair를 모두 저장하지 않고 query별 top-K만 유지
- [ ] self-match 제외 및 동점 score 처리 규칙 정의
- [ ] MAP, MAP@R, Precision@K, Recall@K 구현
- [ ] `K = 1, 5, 10, 100` 결과 계산
- [ ] query별, 문제별 및 macro 평균 출력
- [ ] 작은 fixture에서 ranking과 tie 처리 검증

### 완료 조건

- SC/FV가 동일한 query/candidate 집합에서 평가된다.
- MAP 계산이 POJ retrieval 정의와 일치한다.
- 메모리 사용량이 전체 pair 수에 비례해 증가하지 않는다.

## 4. POJ/CodeNet 기반 Type 1~3 mutation corpus 생성

### Seed 선정

- [ ] 컴파일 가능한 C++ source를 문제별로 균형 추출
- [ ] source 크기와 제어 구조를 고려한 선정 기준 기록
- [ ] 동일 원본과 near-duplicate가 split을 넘지 않도록 분리

### Mutation 생성

- [ ] Type 1: 공백, 줄바꿈, formatting, 주석 변경
- [ ] Type 2: 식별자 rename, literal 및 동등한 타입 표현 변경
- [ ] Type 3: dead code와 문장 삽입·삭제·이동, 제어 구조 일부 치환
- [ ] mutation operation과 strength를 manifest에 기록
- [ ] 원본-변형은 positive, 서로 다른 seed는 negative로 구성

문자열 치환보다 C++ parser 또는 AST 기반 변환을 우선한다.

```csv
seed_id,variant_id,clone_type,operation,strength,split,label
```

### 완료 조건

- Type 1~3별 목표 source와 pair 수를 충족한다.
- 모든 variant가 재현 가능한 mutation provenance를 가진다.
- split 간 seed leakage 검사를 통과한다.

## 5. Mutation 컴파일 및 동작 보존 검증

### 작업

- [ ] 원본과 variant를 동일 compiler/options로 컴파일
- [ ] compiler version, option, timeout과 실패 사유 기록
- [ ] CodeNet metadata 또는 문제별 test input 확보
- [ ] 동일 입력에서 exit code와 표준 출력 비교
- [ ] nondeterministic output과 undefined behavior 제외 기준 정의
- [ ] compile failure, runtime failure와 timeout 분류
- [ ] 검증을 통과한 variant만 최종 benchmark에 포함

```csv
variant_id,compile_status,run_status,output_match,elapsed_ms,failure_reason
```

### 완료 조건

- positive pair가 모두 컴파일 검증을 통과한다.
- 의미 보존 mutation은 실행 결과도 일치한다.
- 제외된 variant의 개수와 사유를 보고한다.

## 6. Source 크기와 변형 강도별 오류 분석

### 작업

- [ ] token 수 또는 source byte 기준 크기 구간 고정
- [ ] clone type과 mutation strength별 성능 계산
- [ ] 크기 구간별 ROC-AUC, PR-AUC와 고정 threshold F1 계산
- [ ] Type 3 operation별 FN 비율 계산
- [ ] negative 종류와 크기 차이에 따른 FP 비율 계산
- [ ] 동일 pair에서 SC/FV 오류 차이 비교
- [ ] 표본이 작은 구간 별도 표시

```text
source size: Q1 이하, Q1-Q2, Q2-Q3, Q3 초과
strength: weak, medium, strong
clone type: Type 1, Type 2, Type 3
```

### 완료 조건

- 성능이 저하되는 source 크기와 mutation 조건을 설명할 수 있다.
- 각 집단의 표본 수와 95% CI를 함께 제시한다.

## 7. 시간 및 peak memory benchmark 실행

### 데이터

- [ ] POJ validation
- [ ] POJ test
- [ ] CodeNet C++1000 20,000 pair
- [ ] `data/courses` 실제 제출물 subset

### 측정 항목

- [ ] source 수, 전체 byte와 DNA token 수
- [ ] DNA 생성시간과 처리량
- [ ] SC/FV 비교시간과 pair/second
- [ ] peak working-set memory
- [ ] 결과 파일 크기, 실패 및 timeout 수
- [ ] cold run과 warm run

### 실행 원칙

- Release build와 동일 PC를 사용하고 환경을 기록한다.
- 각 조건을 최소 5회 반복해 median과 IQR을 보고한다.
- DNA 생성과 pair 비교를 분리해 측정한다.
- SC/FV 실행 순서를 교차해 순서 효과를 줄인다.

### 완료 조건

- 데이터 규모별 시간 및 memory 비교표가 생성된다.
- 동일 조건을 재실행할 수 있는 benchmark wrapper가 제공된다.

## 8. JPlag 등 외부 baseline 추가

### 작업

- [ ] JPlag의 현재 C/C++ 지원, CLI와 라이선스 확인
- [ ] NiCad, SourcererCC 또는 MSCCD 중 재현 가능한 도구 검토
- [ ] 최소 한 도구의 version과 설치 절차 고정
- [ ] cppTR과 동일한 materialized source와 정답 split 사용
- [ ] 결과를 공통 pair ID와 score schema로 변환
- [ ] 점수 방향, 범위, timeout과 미출력 pair 처리 규칙 기록
- [ ] 동일한 bootstrap 및 고정 threshold 평가 적용

### 완료 조건

- 최소 한 개 외부 도구가 POJ와 mutation corpus에서 실행된다.
- cppTR과 외부 도구가 동일한 정답 및 split으로 비교된다.
- 도구별 granularity와 설정 차이를 명시한다.

exEyes 5.0은 실행 환경과 라이선스를 확보하지 못하면 역사적 수치로만 인용한다.

## 9. 대표 FP/FN 수작업 검토

### 표본 추출

- [ ] corpus, clone type, mode와 score 구간별 stratified sample 생성
- [ ] SC만 맞은 pair, FV만 맞은 pair와 공통 오류 포함
- [ ] 높은 점수 FP와 낮은 점수 FN 우선 검토
- [ ] 동일 source의 과도한 중복 방지

### 검토 절차

- [ ] pair identity를 숨긴 blind review format 생성
- [ ] 최소 2명이 독립적으로 clone 여부와 오류 원인 판정
- [ ] 불일치는 합의 또는 제3 검토로 해결
- [ ] Cohen's kappa 등 검토자 간 일치도 계산
- [ ] macro, boilerplate, 입출력 template와 중복 문제 분류
- [ ] 학생 제출물의 개인정보와 공개 범위 검토

### 산출물

```text
manual-review-sample.csv
manual-review-decisions.csv
manual-review-summary.md
```

### 완료 조건

- 대표 FP/FN 원인과 빈도를 설명할 수 있다.
- 문제 ID proxy label의 오류 가능성을 정량적으로 제시한다.
- 공개할 수 없는 source는 비식별화하거나 통계만 보고한다.

## 최종 논문 산출물

- [ ] 데이터와 split 구성표
- [ ] SC/FV 및 외부 baseline 정확도 비교표
- [ ] ROC-AUC, PR-AUC, F1과 95% CI
- [ ] POJ full retrieval 결과
- [ ] Type 1~3과 mutation strength별 결과
- [ ] 실행시간과 peak memory 확장성 결과
- [ ] FP/FN 정성 분석
- [ ] 재현 명령, seed, tool version 및 제한 사항

최종 결론은 `구문적 clone 탐지`, `semantic similarity`, `실제 표절 판정`을 구분한다.
현재 데이터만으로 직접 입증할 수 없는 실제 표절 탐지 성능은 주장하지 않는다.