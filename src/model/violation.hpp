#pragma once

#include <string>
#include <string_view>

namespace ess {

// PLAN.md 3절의 위반 유형표와 1:1로 대응한다.
enum class ViolationCode {
    RateOutOfRange,
    SettleViolation,
    ExclusiveConflict,
    DutyExceeded,
    CooldownViolation,
    PrecedenceViolation,
    CyclicDependency,
};

// "RATE_OUT_OF_RANGE" 같은 대문자 코드 문자열.
std::string_view to_string(ViolationCode code);

struct Violation {
    ViolationCode code;
    std::string step_id;      // 위반이 귀속되는 동작
    std::string related_id;   // 상대 동작 또는 자원 (없으면 빈 문자열)
    std::string resource_id;
    std::string detail;       // 사람이 읽는 설명
};

}  // namespace ess
