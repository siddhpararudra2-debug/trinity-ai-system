#include "trinity/intelligence/Intent.hpp"

namespace trinity::intelligence {

core::Json Intent::toJson() const {
    return core::Json{{"intent_id", intentId},
                      {"domain", domain},
                      {"operation", operation},
                      {"object", object},
                      {"parameters", parameters},
                      {"units", units},
                      {"confidence", confidence}};
}

Intent Intent::fromJson(const core::Json& json) {
    Intent intent;
    intent.intentId = json.value("intent_id", "");
    intent.domain = json.value("domain", "");
    intent.operation = json.value("operation", "");
    intent.object = json.value("object", "");
    intent.parameters = json.value("parameters", core::Json::object());
    intent.units = json.value("units", "");
    intent.confidence = json.value("confidence", 0.0);
    return intent;
}

}  // namespace trinity::intelligence
