#include "model/violation.hpp"

namespace ess {

std::string_view to_string(ViolationCode code) {
    switch (code) {
        case ViolationCode::RateOutOfRange:     return "RATE_OUT_OF_RANGE";
        case ViolationCode::SettleViolation:    return "SETTLE_VIOLATION";
        case ViolationCode::ExclusiveConflict:  return "EXCLUSIVE_CONFLICT";
        case ViolationCode::DutyExceeded:       return "DUTY_EXCEEDED";
        case ViolationCode::CooldownViolation:  return "COOLDOWN_VIOLATION";
        case ViolationCode::PrecedenceViolation:return "PRECEDENCE_VIOLATION";
        case ViolationCode::CyclicDependency:   return "CYCLIC_DEPENDENCY";
    }
    return "UNKNOWN";
}

}  // namespace ess
