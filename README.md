# 장비 동작 시퀀스 시뮬레이터

여러 하드웨어 자원의 물리적 제약을 입력받아, 동작 시퀀스가 제약을 위반하는지 **검증**하고,
위반이 없도록 실행 시각을 **배치**하며, 결과 **타임라인을 출력**하는 C++20 도구입니다.

장비 제어에서 반복적으로 만나는 문제 — *"이 동작들을 어떤 순서와 시각으로 실행해야
어느 자원의 제약도 어기지 않는가"* — 를 도메인 중립적인 모델로 옮겨 놓은 것입니다.

> **모든 예제 수치는 가상의 값입니다.** 특정 제품이나 장비의 실제 스펙과 무관하며,
> 동작을 보이기 위해 임의로 정한 숫자입니다.

---

## 다루는 제약

자원(`Resource`)마다 아래 제약을 걸 수 있고, 동작(`Step`)은 자원 위에서 시간 구간을 차지합니다.

| 제약 | 의미 |
|------|------|
| `rate_min` / `rate_max` | 동작률 허용 범위 |
| `settle_time_ms` | 동작 종료 후 다음 동작까지 필요한 안정화 대기 |
| `max_continuous_ms` | 연속 동작 허용 시간 |
| `cooldown_ms` | 연속 동작 한도 도달 시 필요한 휴지 |
| `exclusive_with` | 동시에 동작할 수 없는 자원 |
| `depends_on` | 선행 동작 완료 조건 (동작 단위) |

검증기는 이 제약들을 7가지 위반 코드로 분류해 보고합니다.

| 코드 | 의미 |
|------|------|
| `RATE_OUT_OF_RANGE` | 동작률이 자원의 허용 범위를 벗어남 |
| `SETTLE_VIOLATION` | 같은 자원의 다음 동작이 안정화 시간 전에 시작 (구간 겹침 포함) |
| `EXCLUSIVE_CONFLICT` | 상호 배타 자원이 시간상 겹쳐서 동작 |
| `DUTY_EXCEEDED` | 연속 동작 시간이 한도를 초과 |
| `COOLDOWN_VIOLATION` | 휴지를 채우지 않고 재동작 |
| `PRECEDENCE_VIOLATION` | 선행 동작이 끝나기 전에 시작 |
| `CYCLIC_DEPENDENCY` | 의존 그래프에 순환이 있어 어떤 배치로도 만족 불가 |

---

## 빠른 시작

외부 의존성은 단일 헤더 두 개(`nlohmann/json`, `doctest`)뿐이고 저장소에 동봉되어 있습니다.
별도로 받을 것이 없습니다.

```bash
cmake -S . -B build
cmake --build build --config Debug

./build/Debug/ess validate scenarios/basic_scan.json          # 배치된 시각을 검사
./build/Debug/ess schedule scenarios/exclusive_conflict.json  # 시각을 자동으로 배치
```

테스트:

```bash
cd build && ctest -C Debug --output-on-failure
```

---

## 실행 예시

### 제약을 모두 만족하는 시퀀스

```
$ ess validate scenarios/basic_scan.json
시나리오: 기본 시퀀스 (모든 수치는 가상 값) (자원 4, 동작 8)
위반 없음.
```

### 제약을 위반하는 시퀀스

```
$ ess validate scenarios/duty_violation.json
시나리오: 제약 위반 예시 (모든 수치는 가상 값) (자원 2, 동작 4)

위반 5건

[RATE_OUT_OF_RANGE] 동작 'spin_3' (자원 'rot_main')
  동작률 20이(가) 자원 'rot_main'의 허용 범위 [1, 12]를 벗어납니다.
[DUTY_EXCEEDED] 동작 'spin_2' (자원 'rot_main')
  연속 동작 2400ms가 한도 2000ms를 초과합니다.
[COOLDOWN_VIOLATION] 동작 'spin_3' (자원 'rot_main')
  연속 동작 한도(2000ms) 도달 후 휴지 1000ms가 필요한데 400ms 만에 다시 동작합니다.
[EXCLUSIVE_CONFLICT] 동작 'emit_1' (자원 'emit_a')
  배타 자원 'emit_a'와(과) 'rot_main'의 동작이 200ms 겹칩니다 ('emit_1' vs 'spin_1').
[EXCLUSIVE_CONFLICT] 동작 'emit_1' (자원 'emit_a')
  배타 자원 'emit_a'와(과) 'rot_main'의 동작이 300ms 겹칩니다 ('emit_1' vs 'spin_2').
```

종료 코드는 `0`(정상) / `1`(위반 또는 스케줄 불가) / `2`(입력 오류) / `3`(내부 오류)로 나뉘어 있어,
스크립트나 CI에서 "검사는 정상 수행됐고 위반이 있다"와 "입력이 잘못됐다"를 구분할 수 있습니다.

### 시각을 자동으로 배치

`start_ms`가 없는 시나리오를 주면 제약을 만족하는 시각을 찾아 채웁니다.

```
$ ess schedule scenarios/exclusive_conflict.json
시나리오: 배타 충돌 자동 회피 (모든 수치는 가상 값) (자원 3, 동작 6)

배치 결과 — 총 소요 4200ms

      시작      종료  자원           동작
         0      1200  rot_main       spin_1
      1200      2100  emit_a         emit_1
      1200      2100  sens_a         read_1
      2100      3300  rot_main       spin_2
      3300      4200  emit_a         emit_2
      3300      4200  sens_a         read_2

자체 검증: 위반 없음
```

이 예제에서 `spin_2`는 선행 조건만 보면 `1400`ms(직전 회전 종료 + 안정화 200ms)에 시작할 수 있지만,
배타 관계인 `emit_a`가 `2100`ms까지 동작하므로 그 뒤로 밀려납니다. 배타 제약을 빼고 돌리면
`1400`ms에 시작하고 총 소요는 `3500`ms가 됩니다.

`-o <파일>`을 주면 시각이 채워진 시나리오를 JSON으로 저장합니다. 저장한 파일은 그대로 다시
`validate`에 넣을 수 있습니다.

**마지막 줄의 `자체 검증`이 이 명령의 핵심입니다.** 스케줄러가 만든 배치를 곧바로 검증기에 다시
넣어 확인합니다. 스케줄러와 검증기는 별개 구현이라 언제든 어긋날 수 있고, 어긋나면 그건 사용자
입력 문제가 아니라 버그이므로 종료 코드 `3`으로 구분해 보고합니다.

배치할 수 없는 입력은 이유를 밝힙니다.

```
$ ess schedule cyclic.json
시나리오: cyclic.json (자원 1, 동작 3)

스케줄 불가 1건

[CYCLIC_DEPENDENCY] 동작 'a'
  의존 관계가 순환해 실행 순서를 정할 수 없습니다: a → b → c → a
```

### 어떤 규칙이 어떤 위반을 내는지

```
$ ess rules
검증 규칙 목록

  rate        RATE_OUT_OF_RANGE
  precedence  PRECEDENCE_VIOLATION
  cycle       CYCLIC_DEPENDENCY
  settle      SETTLE_VIOLATION
  duty        DUTY_EXCEEDED, COOLDOWN_VIOLATION
  exclusive   EXCLUSIVE_CONFLICT
```

이 목록은 손으로 관리하는 문서가 아니라 규칙 타입에서 직접 읽어옵니다. 규칙을 추가하면 자동으로 반영됩니다.

---

## 입력 형식

```json
{
  "name": "기본 시퀀스",
  "resources": [
    {
      "id": "rot_main",
      "name": "주 회전축",
      "type": "Rotary",
      "constraints": {
        "rate_min": 1.0,
        "rate_max": 12.0,
        "settle_time_ms": 200,
        "max_continuous_ms": 4000,
        "cooldown_ms": 1000
      }
    },
    {
      "id": "emit_a",
      "type": "Emitter",
      "constraints": { "exclusive_with": ["rot_main"] }
    }
  ],
  "steps": [
    { "id": "align",  "resource_id": "rot_main", "duration_ms": 1200, "rate": 6.0,
      "start_ms": 900 },
    { "id": "acquire", "resource_id": "emit_a", "duration_ms": 1500, "rate": 2.0,
      "depends_on": ["align"], "start_ms": 2300 }
  ]
}
```

`start_ms`는 선택 항목입니다. `validate`는 이 값을 검사하고, 스케줄러(M2)는 이 값을 채웁니다.
두 명령이 같은 입력 형식을 공유하므로 스케줄러 결과를 그대로 검증기에 다시 넣어 확인할 수 있습니다.

잘못된 입력은 첫 오류에서 멈추지 않고 모아서 위치와 함께 보고합니다.

```
$ ess validate broken.json
입력 오류 3건 — broken.json
  /resources/0/type: 알 수 없는 자원 종류 '회전축' (Rotary, Linear, Emitter, Sensor, Generic 중 하나)
  /steps/0/duration_ms: 필수 항목입니다.
  /steps/1/resource_id: 알 수 없는 자원 'rot_x' (정의된 자원: rot_main, emit_a)
```

---

## 설계 요점

**결정론을 우선했습니다.** 최적해를 찾는 것보다, 제약을 어기지 않는 실행 가능한 해를 항상 같은
순서로 만들어 내는 편이 장비 제어에서 더 쓸모 있습니다. 재현되지 않으면 디버깅할 수 없기 때문입니다.
정렬 기준을 `PlacedStep`의 `<=>` 한 곳에 모아 두었고, 입력 순서를 뒤집어도 결과가 같은지
확인하는 테스트를 두었습니다.

**검증 규칙을 상속 대신 concept으로 묶었습니다.** 규칙끼리 공유할 상태가 없어 기반 클래스가
할 일이 없고, 규칙 목록이 컴파일 타임에 고정되므로 형태가 어긋난 규칙은 실행이 아니라
컴파일에서 걸립니다.

**참조 무결성은 로더의 책임입니다.** id 조회가 실패할 수 있는 지점을 한 곳으로 몰아 두면
검증기와 스케줄러는 본래 로직에만 집중할 수 있습니다.

**스케줄러는 백트래킹하지 않습니다.** 위상 순서대로 한 동작씩 시각을 정하고, 한 번 정한 시각은
바꾸지 않습니다. 동시에 놓을 수 있는 동작이 여럿이면 입력 파일에 먼저 쓴 쪽을 고릅니다 —
사용자가 JSON 순서로 우선순위를 표현할 수 있고, 같은 입력이면 결과가 항상 같습니다.
자원 하나에서는 항상 마지막 동작 뒤에만 배치합니다. 빈 구간에 끼워 넣으면 더 짧은 스케줄이
나오지만, 연속 동작 누적 계산이 앞뒤로 번져 복잡해집니다. 뒤에만 붙이면 그 계산이 검증기와
똑같은 형태가 되어 **결과가 검증을 통과한다는 것이 구조적으로 보장**됩니다.

**시각을 옮겨서 고칠 수 없는 제약은 배치 전에 걸러냅니다.** 동작률 범위 위반이나 "동작 하나가
연속 동작 한도보다 긴" 경우가 그렇습니다. 끝까지 시도한 뒤 실패를 보고하면 원인이 배치 과정에
묻힙니다.

각 결정의 배경은 [DECISIONS.md](DECISIONS.md)에 정리해 두었습니다.

### C++20 사용처

| 기능 | 위치 |
|------|------|
| Concepts | `ValidationRule` — 검증 규칙의 인터페이스를 컴파일 타임에 강제 |
| `<=>` | `Interval`, `PlacedStep` — 정렬 기준을 한 곳에 모아 결정론적 순서 보장 |
| `std::span` | `ScheduleView`의 조회 결과, CLI의 `argv` 전달 |
| Designated initializer | 필드가 많은 모델·설정 구조체 초기화 |
| `std::format` | 위치 정보가 섞인 오류 메시지 조립 |
| Ranges 알고리즘 | 순환 탐색, 배타 목록 정리, 배치 결과 정렬 |

---

## 구조

```
src/
  model/      Resource, Step, Scenario, Interval, 위반 코드, 의존 그래프
  io/         JSON 로더 (오류 수집 및 참조 무결성 검사) / 직렬화
  validate/   ScheduleView + 검증 규칙 6개 + Validator
  schedule/   위상 정렬 기반 시각 배치
  main.cpp    CLI
tests/        doctest 단위 테스트
scenarios/    예제 시나리오
third_party/  단일 헤더 의존성 (nlohmann/json, doctest)
```

순환 검출은 `model/dependency_graph`에 한 번만 구현되어 검증기와 스케줄러가 함께 씁니다.
같은 판정을 두 곳에서 다르게 구현하면 언젠가 어긋나기 때문입니다.

---

## 진행 상황

- [x] **M1 — 모델 · JSON 로더 · 제약 검증기** · `ess validate`, `ess rules`
- [x] **M2 — 스케줄러** · 위상 정렬 + 제약을 만족하는 시각 배치, `ess schedule`
- [ ] **M3 — 시뮬레이션 · 리포트** · 텍스트 간트, CSV 출력, 자원 점유율
- [ ] **M4 — 예제 확장 · 문서 보강**

---

## 빌드 환경

- CMake 3.20 이상, C++20 컴파일러
- 개발·검증 환경: MSVC 19.41 (Visual Studio 2022) / Windows
- 외부 의존성 없음 — 단일 헤더 라이브러리를 저장소에 동봉

`std::format`을 사용합니다. 다른 컴파일러로 옮길 때는 `<format>` 지원 여부를 확인해야 합니다.
