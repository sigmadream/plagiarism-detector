# plagene

C/C++ 소스 코드를 구조만 남긴 토큰열(프로그램 DNA)로 바꾼 뒤 지역 정렬로 비교해, 과제 제출물의 표절 검토 자료를 만드는 CLI 도구입니다.

## 특성과 장점

- 이름과 상수 변경에 강함: 식별자와 리터럴, 주석, 공백은 DNA에 들어가지 않으므로 변수 이름 바꾸기, 상수 바꾸기, 주석 추가, 서식 변경으로는 점수가 내려가지 않습니다.
- 함수 분리와 재배치에 강함: 같은 파일에 정의된 함수는 호출 지점에 본문을 펼쳐 넣습니다(정적 호출 추적). 코드를 함수로 쪼개거나 함수 순서를 바꿔도 실행 흐름 기준으로 비교됩니다.
- 부분 복사 탐지: 전체 파일이 아니라 가장 비슷한 구간을 찾는 지역 정렬(Smith-Waterman)을 쓰므로, 긴 코드 안에 복사한 일부가 섞여 있어도 드러납니다.
- 흔한 코드의 영향 축소: FV 모드는 과제 전체에서 자주 나오는 토큰의 점수를 낮추고 드문 토큰이 일치하면 점수를 높입니다. 모두가 쓰는 입출력 틀보다 개인 고유의 구조가 점수를 좌우합니다.
- 검토 위치 제시: 정렬된 구간의 소스 줄 범위를 함께 출력해, 두 파일의 어디를 나란히 보면 되는지 바로 알 수 있습니다.
- 빠르고 설치가 쉬움: Rust 단일 실행 파일(약 1 MB)이며 설정과 키워드 표가 내장되어 있습니다. Windows, Linux, macOS를 지원하고, DNA 생성과 비교는 CPU 코어 수만큼 병렬로 처리합니다. POJ-104 소스 6,553개의 DNA 생성은 약 3.7초, 4,800쌍 비교는 약 0.5초 걸렸습니다.

## 설치

GitHub Releases에 게시된 압축 파일을 풀어 `plagene`을 PATH에 두면 됩니다. 소스에서 빌드하려면 Rust가 필요합니다.

```sh
cargo build --release   # target/release/plagene
```

## 사용법

두 파일 비교:

```text
$ plagene a.cpp b.cpp
A: a.cpp
B: b.cpp
similarity: 80.896 (FV, CPP)
aligned region: A lines 3-15, B lines 3-13
```

과제 전체 비교(권장, 과제 전체의 토큰 빈도를 반영):

```sh
plagene generate-batch - submissions/ dna/
plagene compare dna/ 1 1 1 1 FV result/ CPP --lines
```

`result/Plag-Detection-Result.csv`에 모든 쌍의 유사도(0~100)와 비슷한 구간의 줄 범위가 기록됩니다. 점수가 높은 쌍부터 해당 줄을 열어 확인하면 됩니다. 전체 옵션은 `plagene --help`에서 볼 수 있습니다.

## 주의

- 점수는 구조적 유사도이며 표절 판정이 아닙니다. 강의에서 제공한 뼈대 코드나 같은 알고리즘의 정석 풀이도 점수가 높게 나오므로 사람이 최종 확인해야 합니다.
- 현재 C와 C++만 지원합니다.

## 저장소 구성

```text
crates/plagene-core       언어 무관 core: DNA 로딩, 토큰 빈도, SC/FV 지역 정렬, 병렬 비교
crates/plagene-frontend   언어별 front end: 소스를 DNA로 변환(현재 C/C++)
crates/plagene-cli        실행 파일 plagene
docs/development.md       개발 문서: 구조, 테스트, 배포, 검증 결과
ref/                      참고 논문
```

## TODO

다중 언어 지원을 위해 결정할 사항입니다. 개발 진행 상황은 [docs/development.md](docs/development.md)에 있습니다.

- [ ] 언어 간 비교: 같은 언어끼리만 비교하면 되나요, 아니면 Python과 C++처럼 서로 다른 언어의 코드끼리도 비교해야 하나요?
- [ ] 우선순위: Python, Haskell, Erlang 중 무엇부터 할까요? 평가에 쓸 제출물 데이터가 있는 언어부터 하는 것을 권합니다.

## License

[MIT](LICENSE)

## Ref

- 지정훈, 적응적 서열 정렬 기법을 이용한 프로그램 유사도 분석 프레임워크, 부산대학교 박사학위논문, 2010
- 김규식 외, 소스코드 유사도 측정 도구의 성능에 관한 비교연구, 한국소프트웨어감정평가학회 논문지 13(1), 2017
