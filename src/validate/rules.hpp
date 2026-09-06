#pragma once

#include <array>
#include <concepts>
#include <span>
#include <string_view>
#include <vector>

#include "model/violation.hpp"
#include "validate/schedule_view.hpp"

namespace ess {

// 검증 규칙이 갖춰야 할 최소 인터페이스.
//
// 규칙을 상속 계층 대신 concept으로 묶은 이유:
//  - 규칙끼리 공유할 상태가 없어 가상 함수와 기반 클래스가 값을 못 한다
//  - 규칙 목록이 컴파일 타임에 고정되므로, 형태가 어긋나면 링크가 아니라 컴파일에서 걸린다
//  - codes를 규칙 자신이 들고 있어, 어떤 규칙이 어떤 위반을 내는지 코드가 곧 문서가 된다
template <typename R>
concept ValidationRule =
    requires(const R& rule, const ScheduleView& view, std::vector<Violation>& out) {
        { R::name } -> std::convertible_to<std::string_view>;
        { R::codes } -> std::convertible_to<std::span<const ViolationCode>>;
        { rule.check(view, out) } -> std::same_as<void>;
    };

// step.rate가 자원의 허용 범위 안인가.
struct RateRule {
    static constexpr std::string_view name = "rate";
    static constexpr std::array codes{ViolationCode::RateOutOfRange};
    void check(const ScheduleView& view, std::vector<Violation>& out) const;
};

// depends_on 선행 동작이 모두 끝난 뒤에 시작하는가.
struct PrecedenceRule {
    static constexpr std::string_view name = "precedence";
    static constexpr std::array codes{ViolationCode::PrecedenceViolation};
    void check(const ScheduleView& view, std::vector<Violation>& out) const;
};

// 의존 그래프에 순환이 있는가. 순환이 있으면 어떤 배치로도 선행 관계를 만족시킬 수 없다.
struct CycleRule {
    static constexpr std::string_view name = "cycle";
    static constexpr std::array codes{ViolationCode::CyclicDependency};
    void check(const ScheduleView& view, std::vector<Violation>& out) const;
};

// 같은 자원에서 이전 동작 종료 후 안정화 시간을 지켰는가. 구간이 겹치는 경우도 여기서 걸린다.
struct SettleRule {
    static constexpr std::string_view name = "settle";
    static constexpr std::array codes{ViolationCode::SettleViolation};
    void check(const ScheduleView& view, std::vector<Violation>& out) const;
};

// 연속 동작 한도와 휴지 조건.
// 두 코드를 한 규칙에 둔 이유: 같은 "연속 구간 누적" 계산에서 나오므로 나누면 계산이 중복된다.
struct DutyRule {
    static constexpr std::string_view name = "duty";
    static constexpr std::array codes{ViolationCode::DutyExceeded, ViolationCode::CooldownViolation};
    void check(const ScheduleView& view, std::vector<Violation>& out) const;
};

// 상호 배타 자원의 동작 구간이 겹치는가.
struct ExclusiveRule {
    static constexpr std::string_view name = "exclusive";
    static constexpr std::array codes{ViolationCode::ExclusiveConflict};
    void check(const ScheduleView& view, std::vector<Violation>& out) const;
};

}  // namespace ess
