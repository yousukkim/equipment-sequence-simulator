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

./build/Debug/ess validate scenarios/basic_scan.json
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

종료 코드는 `0`(위반 없음) / `1`(위반 발견) / `2`(입력 오류)로 나뉘어 있어,
스크립트나 CI에서 "검사는 정상 수행됐고 위반이 있다"와 "입력이 잘못됐다"를 구분할 수 있습니다.

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

각 결정의 배경은 [DECISIONS.md](DECISIONS.md)에 정리해 두었습니다.

### C++20 사용처

| 기능 | 위치 |
|------|------|
| Concepts | `ValidationRule` — 검증 규칙의 인터페이스를 컴파일 타임에 강제 |
| `<=>` | `Interval`, `PlacedStep` — 정렬 기준을 한 곳에 모아 결정론적 순서 보장 |
| `std::span` | `ScheduleView`의 조회 결과, CLI의 `argv` 전달 |
| Designated initializer | 필드가 많은 모델·설정 구조체 초기화 |
| `std::format` | 위치 정보가 섞인 오류 메시지 조립 |

---

## 구조

```
src/
  model/      Resource, Step, Scenario, Interval, 위반 코드 정의
  io/         JSON 로더 (오류 수집 및 참조 무결성 검사)
  validate/   ScheduleView + 검증 규칙 6개 + Validator
  main.cpp    CLI
tests/        doctest 단위 테스트
scenarios/    예제 시나리오
third_party/  단일 헤더 의존성 (nlohmann/json, doctest)
```

---

## 진행 상황

- [x] **M1 — 모델 · JSON 로더 · 제약 검증기** · `ess validate`, `ess rules`
- [ ] **M2 — 스케줄러** · 위상 정렬 + 제약을 만족하는 시각 배치, `ess schedule`
- [ ] **M3 — 시뮬레이션 · 리포트** · 텍스트 간트, CSV 출력, 자원 점유율
- [ ] **M4 — 예제 확장 · 문서 보강**

---

## 빌드 환경

- CMake 3.20 이상, C++20 컴파일러
- 개발·검증 환경: MSVC 19.41 (Visual Studio 2022) / Windows
- 외부 의존성 없음 — 단일 헤더 라이브러리를 저장소에 동봉

`std::format`을 사용합니다. 다른 컴파일러로 옮길 때는 `<format>` 지원 여부를 확인해야 합니다.
