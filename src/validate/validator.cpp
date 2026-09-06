#include "validate/validator.hpp"

#include <type_traits>

namespace ess {

std::vector<Violation> validate(const ScheduleView& view) {
    std::vector<Violation> out;
    // 매개변수를 ValidationRule로 제약해, 규칙 목록에 형태가 맞지 않는 타입이 끼면
    // 실행이 아니라 컴파일에서 걸리게 한다.
    std::apply([&](const ValidationRule auto&... rules) { (rules.check(view, out), ...); },
               DefaultRules{});
    return out;
}

std::vector<RuleInfo> rule_catalog() {
    std::vector<RuleInfo> catalog;
    std::apply(
        [&](const ValidationRule auto&... rules) {
            (catalog.push_back(RuleInfo{
                 .name = std::remove_cvref_t<decltype(rules)>::name,
                 .codes = std::remove_cvref_t<decltype(rules)>::codes,
             }),
             ...);
        },
        DefaultRules{});
    return catalog;
}

}  // namespace ess
