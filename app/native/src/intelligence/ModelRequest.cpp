#include "trinity/intelligence/ModelRequest.hpp"

namespace trinity::intelligence {

core::Json ModelRequest::toJson() const {
    return core::Json{{"prompt", prompt},
                      {"temperature", temperature},
                      {"top_p", topP},
                      {"max_tokens", maxTokens}};
}

}  // namespace trinity::intelligence
