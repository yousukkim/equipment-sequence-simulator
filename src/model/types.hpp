#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ess {

// 시각과 기간은 모두 밀리초 정수로 다룬다.
// 부동소수 누적 오차 없이 비교·정렬하기 위한 선택이다.
using Ms = std::int64_t;

enum class ResourceType { Rotary, Linear, Emitter, Sensor, Generic };

std::string_view to_string(ResourceType type);
std::optional<ResourceType> parse_resource_type(std::string_view text);

// 자원 하나에 걸린 물리적 제약.
struct Constraints {
    // 동작률 허용 범위. 기본값은 "제한 없음"이다.
    double rate_min = 0.0;
    double rate_max = std::numeric_limits<double>::infinity();

    Ms settle_time_ms = 0;     // 동작 종료 후 다음 동작까지 필요한 안정화 대기
    Ms max_continuous_ms = 0;  // 연속 동작 허용 시간 (0 = 무제한)
    Ms cooldown_ms = 0;        // 연속 동작 한도 도달 시 필요한 휴지이자, 연속 여부를 가르는 간격

    std::vector<std::string> exclusive_with;  // 동시 동작 불가 자원
};

struct Resource {
    std::string id;
    std::string name;
    ResourceType type = ResourceType::Generic;
    Constraints constraints;
};

struct Step {
    std::string id;
    std::string resource_id;
    Ms duration_ms = 0;
    double rate = 0.0;
    std::vector<std::string> depends_on;

    // 시각 배치. M1의 validate는 이 값이 채워진 시나리오를 검사하고,
    // M2의 scheduler가 비어 있는 값을 채우게 된다.
    std::optional<Ms> start_ms;
};

struct Scenario {
    std::string name;
    std::vector<Resource> resources;
    std::vector<Step> steps;
};

// 시간 축 위의 반열린 구간 [start, end).
struct Interval {
    Ms start = 0;
    Ms end = 0;

    // 비교 규칙을 한 곳에 모아 두면 정렬 결과가 흔들리지 않는다.
    auto operator<=>(const Interval&) const = default;

    constexpr Ms length() const { return end - start; }
    constexpr bool overlaps(const Interval& other) const {
        return start < other.end && other.start < end;
    }
};

// 시각이 확정된 동작.
struct PlacedStep {
    Interval when;
    const Step* step = nullptr;

    // 시각이 같아도 순서가 바뀌지 않도록 id까지 비교한다.
    // 포인터 값으로 비교하면 실행할 때마다 순서가 달라질 수 있다.
    std::strong_ordering operator<=>(const PlacedStep& other) const {
        if (auto cmp = when <=> other.when; cmp != std::strong_ordering::equal) {
            return cmp;
        }
        return step->id <=> other.step->id;
    }
    bool operator==(const PlacedStep& other) const {
        return (*this <=> other) == std::strong_ordering::equal;
    }
};

}  // namespace ess
