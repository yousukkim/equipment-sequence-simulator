#include "model/types.hpp"

#include <array>
#include <utility>

namespace ess {
namespace {

// 문자열 표기는 JSON 입력과 출력 양쪽에서 같은 표를 쓴다.
constexpr std::array<std::pair<ResourceType, std::string_view>, 5> kResourceTypeNames{{
    {ResourceType::Rotary, "Rotary"},
    {ResourceType::Linear, "Linear"},
    {ResourceType::Emitter, "Emitter"},
    {ResourceType::Sensor, "Sensor"},
    {ResourceType::Generic, "Generic"},
}};

}  // namespace

std::string_view to_string(ResourceType type) {
    for (const auto& [value, name] : kResourceTypeNames) {
        if (value == type) return name;
    }
    return "Generic";
}

std::optional<ResourceType> parse_resource_type(std::string_view text) {
    for (const auto& [value, name] : kResourceTypeNames) {
        if (name == text) return value;
    }
    return std::nullopt;
}

}  // namespace ess
