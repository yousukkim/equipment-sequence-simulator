#pragma once

#include <span>
#include <string_view>
#include <tuple>
#include <vector>

#include "model/violation.hpp"
#include "validate/rules.hpp"
#include "validate/schedule_view.hpp"

namespace ess {

// 실행할 규칙 목록. 여기에 추가하기만 하면 validate()와 rule_catalog()가 함께 반영된다.
// 순서를 고정해 두었으므로 같은 입력이면 항상 같은 순서의 결과가 나온다.
using DefaultRules =
    std::tuple<RateRule, PrecedenceRule, CycleRule, SettleRule, DutyRule, ExclusiveRule>;

std::vector<Violation> validate(const ScheduleView& view);

struct RuleInfo {
    std::string_view name;
    std::span<const ViolationCode> codes;
};

// 어떤 규칙이 어떤 위반 코드를 내는지. `ess rules`가 이 목록을 출력한다.
std::vector<RuleInfo> rule_catalog();

}  // namespace ess
