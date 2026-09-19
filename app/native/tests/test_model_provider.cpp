#include <doctest.h>

#include "trinity/intelligence/IModelProvider.hpp"

TEST_CASE("null provider is unavailable and refuses to plan") {
    trinity::intelligence::NullModelProvider provider;
    const auto info = provider.info();
    CHECK_FALSE(info.available);
    CHECK_FALSE(info.configured);

    trinity::intelligence::ModelRequest request;
    request.prompt = "Create a 50 mm quadcopter frame";
    const auto result = provider.generatePlan(request);
    REQUIRE(result.isOk());
    CHECK_FALSE(result.value().success);
    CHECK(result.value().error["code"] == "capability_unavailable");

    int chunks = 0;
    bool sawFinal = false;
    provider.streamPlan(request, [&](const std::string&, bool final) {
        ++chunks;
        sawFinal = sawFinal || final;
    });
    CHECK(chunks == 1);
    CHECK(sawFinal);

    CHECK(provider.configure(trinity::core::Json::object()).isOk());
}
