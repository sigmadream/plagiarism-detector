# Baseline results

실행일: 2026-07-20

## 환경

- OS: Windows
- cppTR build: MSVC Debug
- cppTR options: `1 1 1 1`
- random seed: `20260720`
- positive/negative ratio: `1:1`
- language: `CPP`

## POJ-104

### Validation pilot

- 문제: 17
- source: 1,256
- pair: 680 (positive 340, negative 340)

| mode | ROC-AUC | PR-AUC | pilot threshold | precision | recall | F1 |
|---|---:|---:|---:|---:|---:|---:|
| SC | 0.744161 | 0.766782 | 13.5593 | 0.587426 | 0.879412 | 0.704358 |
| FV | 0.773296 | 0.799908 | 20.2005 | 0.618844 | 0.850000 | 0.716233 |

각 mode의 pilot threshold를 test 결과를 보기 전에 고정했다.

### Test

- 문제: 24
- source: 6,553
- pair: 4,800 (positive 2,400, negative 2,400)
- DNA generation failure: 0

| mode | ROC-AUC | PR-AUC | fixed threshold | precision | recall | F1 |
|---|---:|---:|---:|---:|---:|---:|
| SC | 0.773234 | 0.775248 | 13.5593 | 0.563910 | 0.937500 | 0.704225 |
| FV | 0.799635 | 0.810255 | 20.2005 | 0.596999 | 0.911667 | 0.721517 |

고정 threshold 50도 함께 비교했다.

| mode | precision@50 | recall@50 | F1@50 |
|---|---:|---:|---:|
| SC | 0.911330 | 0.154167 | 0.263721 |
| FV | 0.896719 | 0.307500 | 0.457958 |

FV는 test에서 SC보다 ROC-AUC, PR-AUC, validation 고정 threshold F1이 모두 높았다.
threshold 50은 precision이 높지만 recall 손실이 크므로 이 corpus의 기본 운영값으로 보기
어렵다.

`best_f1_exploratory`는 test label로 선택되는 값이므로 위 최종 표에 사용하지 않았다.

## Project CodeNet C++1400

현재 workspace에 C++1400 원본 corpus가 없어 실행하지 않았다. `benchmark.py`의 CodeNet
adapter와 전체 sampling/materialization 흐름은 작은 `pNNNNN/*.cpp` 회귀 fixture로
검증했다. 공식 corpus를 [README.md](README.md)의 입력 경로에 배치한 뒤 동일 seed와
SC/FV 설정으로 실행해야 한다.

## 해석 제한

같은 문제의 제출물은 실제 표절 pair가 아니다. 이 결과는 문제 ID를 proxy label로 사용한
C++ semantic similarity 성능이며, 표절 탐지 precision/recall로 인용하면 안 된다.