#pragma once

#include <vector>

#include "model/types.hpp"
#include "model/violation.hpp"

namespace ess {

struct ScheduleResult {
    Scenario scenario;                // 성공하면 모든 동작에 start_ms가 채워져 있다
    std::vector<Violation> failures;  // 비어 있으면 성공

    bool ok() const { return failures.empty(); }
};

// 입력의 start_ms를 무시하고 제약을 만족하는 시각을 새로 배치한다.
//
// 알고리즘:
//  0. 시각을 옮겨서는 고칠 수 없는 제약을 먼저 걸러낸다
//     (동작률 범위 위반, 단일 동작 길이가 연속 동작 한도 초과)
//  1. 선행 관계로 위상 정렬한다. 순환이면 CYCLIC_DEPENDENCY로 실패한다
//  2. 위상 순서대로 각 동작을 아래 하한들 중 가장 늦은 시각에 놓는다
//     - 선행 동작들의 완료 시각
//     - 같은 자원의 직전 동작 종료 + 안정화 시간
//     - 연속 동작 한도에 걸리면 직전 동작 종료 + 휴지 시간
//     그다음 배타 자원의 구간과 겹치지 않을 때까지 뒤로 민다
//  3. 시각을 확정하고 다음 동작으로 넘어간다
//
// 되돌아가지 않는다(백트래킹 없음). 한 번 정한 시각은 바꾸지 않으므로 결과가 항상 같다.
ScheduleResult schedule(const Scenario& scenario);

}  // namespace ess
