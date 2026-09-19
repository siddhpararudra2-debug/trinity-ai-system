#pragma once

// Trinity core JSON alias. nlohmann/json is vendored under
// app/native/third_party/json and carries no runtime dependency.
#include <json.hpp>

namespace trinity::core {

using Json = nlohmann::json;

}  // namespace trinity::core
