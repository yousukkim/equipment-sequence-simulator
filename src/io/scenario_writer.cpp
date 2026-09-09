#include "io/scenario_writer.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>
#include <utility>

namespace ess {
namespace {

using nlohmann::ordered_json;

ordered_json constraints_to_json(const Constraints& c) {
    ordered_json out = ordered_json::object();
    if (c.rate_min != 0.0) out["rate_min"] = c.rate_min;
    if (std::isfinite(c.rate_max)) out["rate_max"] = c.rate_max;
    if (c.settle_time_ms != 0) out["settle_time_ms"] = c.settle_time_ms;
    if (c.max_continuous_ms != 0) out["max_continuous_ms"] = c.max_continuous_ms;
    if (c.cooldown_ms != 0) out["cooldown_ms"] = c.cooldown_ms;
    if (!c.exclusive_with.empty()) out["exclusive_with"] = c.exclusive_with;
    return out;
}

}  // namespace

std::string to_json_text(const Scenario& scenario) {
    // 필드 순서를 입력 파일과 같게 유지하려고 ordered_json을 쓴다.
    ordered_json root = ordered_json::object();
    if (!scenario.name.empty()) root["name"] = scenario.name;

    ordered_json resources = ordered_json::array();
    for (const Resource& resource : scenario.resources) {
        ordered_json node = ordered_json::object();
        node["id"] = resource.id;
        if (!resource.name.empty() && resource.name != resource.id) node["name"] = resource.name;
        node["type"] = std::string{to_string(resource.type)};
        if (ordered_json constraints = constraints_to_json(resource.constraints);
            !constraints.empty()) {
            node["constraints"] = std::move(constraints);
        }
        resources.push_back(std::move(node));
    }
    root["resources"] = std::move(resources);

    ordered_json steps = ordered_json::array();
    for (const Step& step : scenario.steps) {
        ordered_json node = ordered_json::object();
        node["id"] = step.id;
        node["resource_id"] = step.resource_id;
        node["duration_ms"] = step.duration_ms;
        node["rate"] = step.rate;
        if (!step.depends_on.empty()) node["depends_on"] = step.depends_on;
        if (step.start_ms.has_value()) node["start_ms"] = *step.start_ms;
        steps.push_back(std::move(node));
    }
    root["steps"] = std::move(steps);

    return root.dump(2) + "\n";
}

}  // namespace ess
