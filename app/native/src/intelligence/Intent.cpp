#include "trinity/intelligence/Intent.hpp"

namespace trinity::intelligence {

core::Json Intent::toJson() const {
    return core::Json{{"domain", domain},
                      {"operation", operation},
                      {"object", object},
                      {"parameters", parameters},
                      {"units", units}};
}

}  // namespace trinity::intelligence
