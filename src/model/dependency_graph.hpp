#pragma once

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "model/types.hpp"

namespace ess {

// 동작 사이의 선행 관계 그래프. 간선 방향은 "선행 → 후행"이다.
// 검증기(순환 보고)와 스케줄러(위상 정렬)가 같은 구현을 쓰도록 여기로 모았다.

// 발견된 순환 목록. 각 순환은 가장 작은 id에서 시작하도록 회전시켜 중복을 없앤다.
// 어디서 발견하든 같은 순환은 같은 표현이 된다.
std::set<std::vector<std::string>> find_dependency_cycles(const Scenario& scenario);

// 위상 순서 (scenario.steps의 인덱스). 순환이 있으면 nullopt.
// 동시에 놓을 수 있는 동작이 여럿이면 입력 순서가 빠른 쪽을 먼저 꺼낸다.
std::optional<std::vector<std::size_t>> topological_order(const Scenario& scenario);

}  // namespace ess
