#pragma once

#include <string>

#include "model/types.hpp"

namespace ess {

// 시나리오를 다시 읽어들일 수 있는 JSON으로 직렬화한다.
// 기본값인 항목은 생략해 출력이 짧게 유지된다.
// (특히 rate_max의 기본값은 무한대라 JSON으로 표현할 수 없으므로 반드시 생략해야 한다.)
std::string to_json_text(const Scenario& scenario);

}  // namespace ess
