#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "model/types.hpp"

namespace ess {

struct LoadError {
    std::string where;    // JSON 위치 (예: /steps/2/rate)
    std::string message;  // 사람이 읽는 설명
};

struct LoadResult {
    Scenario scenario;
    std::vector<LoadError> errors;

    bool ok() const { return errors.empty(); }
};

// 첫 오류에서 멈추지 않고 가능한 모든 오류를 모아서 돌려준다.
// 입력 파일을 한 번 고칠 때 최대한 많은 문제를 볼 수 있어야 하기 때문이다.
LoadResult load_scenario_text(std::string_view text);
LoadResult load_scenario_file(const std::filesystem::path& path);

std::string format_errors(const std::vector<LoadError>& errors);

}  // namespace ess
