#include <doctest.h>

#include <stdexcept>

#include "trinity/workflows/Workflow.hpp"

TEST_CASE("workflow orders dependencies before dependents") {
    using trinity::workflows::WorkflowNode;
    const auto plan = trinity::workflows::ordered({
        {"c", "cad", "generate", {}, {"b"}},
        {"a", "math", "solve", {}, {}},
        {"b", "cad", "validate", {}, {"a"}},
    });
    REQUIRE(plan.size() == 3);
    CHECK(plan[0].id == "a");
    CHECK(plan[1].id == "b");
    CHECK(plan[2].id == "c");
}

TEST_CASE("workflow rejects duplicate node ids") {
    using trinity::workflows::WorkflowNode;
    CHECK_THROWS_WITH_AS(
        trinity::workflows::ordered(
            {{"build", "cad", "generate", {}, {}}, {"build", "cad", "validate", {}, {}}}),
        "Workflow has duplicate node IDs", std::invalid_argument);
}

TEST_CASE("workflow rejects cycles and missing dependencies") {
    using trinity::workflows::WorkflowNode;
    CHECK_THROWS_AS(trinity::workflows::ordered({{"a", "cad", "generate", {}, {"b"}},
                                                 {"b", "cad", "validate", {}, {"a"}}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        trinity::workflows::ordered({{"a", "cad", "generate", {}, {"ghost"}}}),
        std::invalid_argument);
}
