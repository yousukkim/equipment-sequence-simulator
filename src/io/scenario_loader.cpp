#include "io/scenario_loader.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_set>

namespace ess {
namespace {

using nlohmann::json;

std::string join(const std::vector<std::string>& items, std::string_view sep = ", ") {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) out += sep;
        out += items[i];
    }
    return out;
}

// 로더 상태를 한 곳에 모아 둔다. 각 read_* 는 실패하면 오류를 남기고 nullopt를 돌려준다.
class Loader {
public:
    LoadResult run(const json& root) {
        parse_root(root);
        return LoadResult{.scenario = std::move(scenario_), .errors = std::move(errors_)};
    }

private:
    Scenario scenario_;
    std::vector<LoadError> errors_;

    void add_error(std::string where, std::string message) {
        errors_.push_back(LoadError{.where = std::move(where), .message = std::move(message)});
    }

    // ---- 필드 읽기 헬퍼 ---------------------------------------------------

    static const json* member(const json& node, const char* key) {
        const auto it = node.find(key);
        return it == node.end() ? nullptr : &(*it);
    }

    std::optional<std::string> read_string(const json& node, const char* key,
                                           const std::string& where, bool required) {
        const json* value = member(node, key);
        if (value == nullptr) {
            if (required) add_error(where + "/" + key, "필수 항목입니다.");
            return std::nullopt;
        }
        if (!value->is_string()) {
            add_error(where + "/" + key, "문자열이어야 합니다.");
            return std::nullopt;
        }
        std::string text = value->get<std::string>();
        if (required && text.empty()) {
            add_error(where + "/" + key, "빈 문자열은 쓸 수 없습니다.");
            return std::nullopt;
        }
        return text;
    }

    std::optional<double> read_number(const json& node, const char* key,
                                      const std::string& where, bool required) {
        const json* value = member(node, key);
        if (value == nullptr) {
            if (required) add_error(where + "/" + key, "필수 항목입니다.");
            return std::nullopt;
        }
        if (!value->is_number()) {
            add_error(where + "/" + key, "숫자여야 합니다.");
            return std::nullopt;
        }
        return value->get<double>();
    }

    // 시간 값은 밀리초 정수이며 음수를 허용하지 않는다.
    std::optional<Ms> read_ms(const json& node, const char* key, const std::string& where,
                              bool required) {
        const json* value = member(node, key);
        if (value == nullptr) {
            if (required) add_error(where + "/" + key, "필수 항목입니다.");
            return std::nullopt;
        }
        if (!value->is_number_integer()) {
            add_error(where + "/" + key, "밀리초 정수여야 합니다.");
            return std::nullopt;
        }
        const auto ms = value->get<std::int64_t>();
        if (ms < 0) {
            add_error(where + "/" + key, std::format("음수({})는 쓸 수 없습니다.", ms));
            return std::nullopt;
        }
        return ms;
    }

    std::vector<std::string> read_string_array(const json& node, const char* key,
                                               const std::string& where) {
        std::vector<std::string> out;
        const json* value = member(node, key);
        if (value == nullptr) return out;
        if (!value->is_array()) {
            add_error(where + "/" + key, "문자열 배열이어야 합니다.");
            return out;
        }
        for (std::size_t i = 0; i < value->size(); ++i) {
            const json& item = (*value)[i];
            const std::string item_where = std::format("{}/{}/{}", where, key, i);
            if (!item.is_string() || item.get<std::string>().empty()) {
                add_error(item_where, "비어 있지 않은 문자열이어야 합니다.");
                continue;
            }
            out.push_back(item.get<std::string>());
        }
        return out;
    }

    // ---- 구조 읽기 --------------------------------------------------------

    void parse_root(const json& root) {
        if (!root.is_object()) {
            add_error("/", "최상위는 resources와 steps를 가진 객체여야 합니다.");
            return;
        }
        if (const json* name = member(root, "name"); name != nullptr) {
            if (name->is_string()) {
                scenario_.name = name->get<std::string>();
            } else {
                add_error("/name", "문자열이어야 합니다.");
            }
        }
        parse_resources(root);
        parse_steps(root);
        cross_check();
    }

    void parse_resources(const json& root) {
        const json* node = member(root, "resources");
        if (node == nullptr) {
            add_error("/resources", "필수 항목입니다. 자원 배열이 없습니다.");
            return;
        }
        if (!node->is_array()) {
            add_error("/resources", "배열이어야 합니다.");
            return;
        }
        if (node->empty()) {
            add_error("/resources", "자원이 하나 이상 필요합니다.");
            return;
        }

        std::unordered_set<std::string> seen;
        for (std::size_t i = 0; i < node->size(); ++i) {
            const json& item = (*node)[i];
            const std::string where = std::format("/resources/{}", i);
            if (!item.is_object()) {
                add_error(where, "객체여야 합니다.");
                continue;
            }
            const auto id = read_string(item, "id", where, true);
            if (!id) continue;  // id를 모르면 나머지 오류를 가리켜도 읽는 사람이 못 찾는다
            if (!seen.insert(*id).second) {
                add_error(where + "/id", std::format("자원 id '{}'가 중복됩니다.", *id));
                continue;
            }

            Resource resource{
                .id = *id,
                .name = read_string(item, "name", where, false).value_or(*id),
                .type = ResourceType::Generic,
                .constraints = {},
            };
            if (const json* type = member(item, "type"); type != nullptr) {
                if (!type->is_string()) {
                    add_error(where + "/type", "문자열이어야 합니다.");
                } else if (const auto parsed = parse_resource_type(type->get<std::string>())) {
                    resource.type = *parsed;
                } else {
                    add_error(where + "/type",
                              std::format("알 수 없는 자원 종류 '{}' "
                                          "(Rotary, Linear, Emitter, Sensor, Generic 중 하나)",
                                          type->get<std::string>()));
                }
            }
            parse_constraints(item, where, resource.constraints);
            scenario_.resources.push_back(std::move(resource));
        }
    }

    void parse_constraints(const json& item, const std::string& where, Constraints& out) {
        const json* node = member(item, "constraints");
        if (node == nullptr) return;  // 제약 없음 = 무제약
        if (!node->is_object()) {
            add_error(where + "/constraints", "객체여야 합니다.");
            return;
        }
        const std::string cw = where + "/constraints";

        if (const auto v = read_number(*node, "rate_min", cw, false)) out.rate_min = *v;
        if (const auto v = read_number(*node, "rate_max", cw, false)) out.rate_max = *v;
        if (const auto v = read_ms(*node, "settle_time_ms", cw, false)) out.settle_time_ms = *v;
        if (const auto v = read_ms(*node, "max_continuous_ms", cw, false)) out.max_continuous_ms = *v;
        if (const auto v = read_ms(*node, "cooldown_ms", cw, false)) out.cooldown_ms = *v;
        out.exclusive_with = read_string_array(*node, "exclusive_with", cw);

        if (out.rate_min < 0.0) {
            add_error(cw + "/rate_min", std::format("음수({})는 쓸 수 없습니다.", out.rate_min));
        }
        if (out.rate_min > out.rate_max) {
            add_error(cw, std::format("rate_min({})이 rate_max({})보다 큽니다.", out.rate_min,
                                      out.rate_max));
        }
        // cooldown_ms는 "휴지로 인정되는 최소 간격"이기도 하다.
        // 0이면 어떤 간격이든 휴지로 인정되어 연속 동작 한도가 무의미해진다.
        if (out.max_continuous_ms > 0 && out.cooldown_ms <= 0) {
            add_error(cw,
                      "max_continuous_ms를 쓰려면 cooldown_ms가 1 이상이어야 합니다. "
                      "cooldown_ms는 연속 동작이 끊겼다고 볼 최소 간격이기도 합니다.");
        }
    }

    void parse_steps(const json& root) {
        const json* node = member(root, "steps");
        if (node == nullptr) {
            add_error("/steps", "필수 항목입니다. 동작 배열이 없습니다.");
            return;
        }
        if (!node->is_array()) {
            add_error("/steps", "배열이어야 합니다.");
            return;
        }

        std::unordered_set<std::string> seen;
        for (std::size_t i = 0; i < node->size(); ++i) {
            const json& item = (*node)[i];
            const std::string where = std::format("/steps/{}", i);
            if (!item.is_object()) {
                add_error(where, "객체여야 합니다.");
                continue;
            }
            const auto id = read_string(item, "id", where, true);
            if (!id) continue;
            if (!seen.insert(*id).second) {
                add_error(where + "/id", std::format("동작 id '{}'가 중복됩니다.", *id));
                continue;
            }

            Step step{
                .id = *id,
                .resource_id = read_string(item, "resource_id", where, true).value_or(""),
                .duration_ms = read_ms(item, "duration_ms", where, true).value_or(0),
                .rate = read_number(item, "rate", where, true).value_or(0.0),
                .depends_on = read_string_array(item, "depends_on", where),
                .start_ms = read_ms(item, "start_ms", where, false),
            };
            if (step.duration_ms == 0 && member(item, "duration_ms") != nullptr) {
                add_error(where + "/duration_ms",
                          "1 이상이어야 합니다. 길이가 0인 동작은 다루지 않습니다.");
            }
            scenario_.steps.push_back(std::move(step));
        }
    }

    // 참조 무결성 검사.
    // 여기를 통과하면 이후 단계는 id 조회가 항상 성공한다고 가정할 수 있다.
    void cross_check() {
        std::vector<std::string> resource_ids;
        std::unordered_set<std::string> resource_set;
        for (const Resource& r : scenario_.resources) {
            resource_ids.push_back(r.id);
            resource_set.insert(r.id);
        }
        std::unordered_set<std::string> step_set;
        for (const Step& s : scenario_.steps) step_set.insert(s.id);

        for (std::size_t i = 0; i < scenario_.resources.size(); ++i) {
            const Resource& r = scenario_.resources[i];
            for (std::size_t j = 0; j < r.constraints.exclusive_with.size(); ++j) {
                const std::string& other = r.constraints.exclusive_with[j];
                const std::string where =
                    std::format("/resources/{}/constraints/exclusive_with/{}", i, j);
                if (other == r.id) {
                    add_error(where,
                              std::format("자기 자신('{}')과 배타 관계를 지정했습니다.", other));
                } else if (!resource_set.contains(other)) {
                    add_error(where, std::format("알 수 없는 자원 '{}' (정의된 자원: {})", other,
                                                 join(resource_ids)));
                }
            }
        }

        for (std::size_t i = 0; i < scenario_.steps.size(); ++i) {
            const Step& s = scenario_.steps[i];
            const std::string where = std::format("/steps/{}", i);
            if (!s.resource_id.empty() && !resource_set.contains(s.resource_id)) {
                add_error(where + "/resource_id",
                          std::format("알 수 없는 자원 '{}' (정의된 자원: {})", s.resource_id,
                                      join(resource_ids)));
            }
            std::unordered_set<std::string> dep_seen;
            for (std::size_t j = 0; j < s.depends_on.size(); ++j) {
                const std::string& dep = s.depends_on[j];
                const std::string dep_where = std::format("{}/depends_on/{}", where, j);
                if (dep == s.id) {
                    add_error(dep_where, "자기 자신에 의존할 수 없습니다.");
                } else if (!step_set.contains(dep)) {
                    add_error(dep_where, std::format("알 수 없는 동작 '{}'", dep));
                } else if (!dep_seen.insert(dep).second) {
                    add_error(dep_where, std::format("'{}'가 중복 지정되었습니다.", dep));
                }
            }
        }
    }
};

}  // namespace

LoadResult load_scenario_text(std::string_view text) {
    json root;
    try {
        root = json::parse(text);
    } catch (const json::parse_error& e) {
        return LoadResult{
            .scenario = {},
            .errors = {LoadError{.where = std::format("byte {}", e.byte),
                                 .message = std::format("JSON 구문 오류: {}", e.what())}},
        };
    }
    return Loader{}.run(root);
}

LoadResult load_scenario_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return LoadResult{
            .scenario = {},
            .errors = {LoadError{.where = path.string(), .message = "파일을 열 수 없습니다."}},
        };
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return load_scenario_text(buffer.str());
}

std::string format_errors(const std::vector<LoadError>& errors) {
    std::string out;
    for (const LoadError& e : errors) {
        out += std::format("  {}: {}\n", e.where, e.message);
    }
    return out;
}

}  // namespace ess
