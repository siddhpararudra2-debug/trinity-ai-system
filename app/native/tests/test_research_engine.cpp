#include <doctest.h>

#include <filesystem>

#include "trinity/engines/ResearchEngine.hpp"

using trinity::engines::EngineRequest;
using trinity::engines::ResearchEngine;

namespace {

EngineRequest makeReq(const std::string& operation, trinity::core::Json params) {
    EngineRequest req;
    req.engine = "research";
    req.operation = operation;
    req.parameters = std::move(params);
    return req;
}

}  // namespace

TEST_CASE("research describes capabilities") {
    ResearchEngine engine;
    CHECK(engine.name() == "research");
    const auto out = engine.execute(makeReq("describe", {}));
    REQUIRE(out.success);
    CHECK(out.result["engine"] == "research");
    REQUIRE(out.result["capabilities"].is_array());
    CHECK(out.result["capabilities"].size() == 7);
    REQUIRE(out.validation.has_value());
    CHECK(out.validation->passed());
}

TEST_CASE("research indexes documents and searches deterministically") {
    ResearchEngine engine;
    const auto first = engine.execute(makeReq("index_document", {{"doc_id", "doc-a"},
                                                                 {"title", "Battery lifetime tests"},
                                                                 {"text", "Battery lifetime tests "
                                                                          "measure discharge cycles "
                                                                          "under load."}}));
    REQUIRE(first.success);
    CHECK(first.result["document_count"] == 1);
    CHECK(first.result["doc_id"] == "doc-a");

    const auto second = engine.execute(makeReq("index_document", {{"doc_id", "doc-b"},
                                                                  {"title", "Motor winding guide"},
                                                                  {"text", "Motor winding uses "
                                                                           "copper wire and "
                                                                           "insulation."}}));
    REQUIRE(second.success);
    CHECK(second.result["document_count"] == 2);

    const auto search = engine.execute(makeReq("search", {{"query", "battery discharge"}}));
    REQUIRE(search.success);
    CHECK(search.result["hit_count"] == 1);
    REQUIRE(search.result["hits"].is_array());
    REQUIRE(search.result["hits"].size() == 1);
    CHECK(search.result["hits"][0]["doc_id"] == "doc-a");
    CHECK(search.result["hits"][0]["rank"] == 1);
    CHECK(search.result["hits"][0]["score"].get<double>() > 0.0);
    REQUIRE(search.validation.has_value());
    CHECK(search.validation->passed());

    // Same query twice: identical deterministic output.
    const auto again = engine.execute(makeReq("search", {{"query", "battery discharge"}}));
    REQUIRE(again.success);
    CHECK(again.result["hits"] == search.result["hits"]);
}

TEST_CASE("research search boosts title matches and ranks by score") {
    ResearchEngine engine;
    engine.execute(makeReq("index_document", {{"doc_id", "title-hit"},
                                              {"title", "Solar inverter design"},
                                              {"text", "Notes about voltages."}}));
    engine.execute(makeReq("index_document", {{"doc_id", "body-hit"},
                                              {"title", "Unrelated heading"},
                                              {"text", "A design note mentioning inverter "
                                                       "topologies in passing."}}));
    const auto out = engine.execute(makeReq("search", {{"query", "inverter design"}}));
    REQUIRE(out.success);
    REQUIRE(out.result["hits"].size() == 2);
    CHECK(out.result["hits"][0]["doc_id"] == "title-hit");
    CHECK(out.result["hits"][0]["score"].get<double>() >=
          out.result["hits"][1]["score"].get<double>());
}

TEST_CASE("research search on empty index succeeds with zero hits") {
    ResearchEngine engine;
    const auto out = engine.execute(makeReq("search", {{"query", "anything here"}}));
    REQUIRE(out.success);
    CHECK(out.result["hit_count"] == 0);
    CHECK(out.result["hits"].empty());
    CHECK(out.result["total_documents"] == 0);
}

TEST_CASE("research refuses duplicate ids and missing parameters truthfully") {
    ResearchEngine engine;
    engine.execute(makeReq("index_document", {{"doc_id", "dup"},
                                              {"title", "One"},
                                              {"text", "First body text here."}}));
    const auto dup = engine.execute(makeReq("index_document", {{"doc_id", "dup"},
                                                               {"title", "Two"},
                                                               {"text", "Second body text here."}}));
    CHECK_FALSE(dup.success);
    REQUIRE_FALSE(dup.errors.empty());

    const auto noTitle = engine.execute(makeReq("index_document", {{"text", "body only"}}));
    CHECK_FALSE(noTitle.success);
    REQUIRE_FALSE(noTitle.errors.empty());

    const auto noQuery = engine.execute(makeReq("search", {}));
    CHECK_FALSE(noQuery.success);
    REQUIRE_FALSE(noQuery.errors.empty());

    const auto badLimit = engine.execute(makeReq("search", {{"query", "x"}, {"limit", 999}}));
    CHECK_FALSE(badLimit.success);
    REQUIRE_FALSE(badLimit.errors.empty());
}

TEST_CASE("research refuses unsupported operations without faking results") {
    ResearchEngine engine;
    for (const std::string& op : {"web_search", "crawl", "llm_summarize"}) {
        const auto out = engine.execute(makeReq(op, {}));
        CHECK_FALSE(out.success);
        REQUIRE_FALSE(out.errors.empty());
        REQUIRE(out.validation.has_value());
        CHECK_FALSE(out.validation->passed());
    }
}

TEST_CASE("research summarize returns extractive sentences with provenance") {
    ResearchEngine engine;
    engine.execute(makeReq("index_document",
                           {{"doc_id", "notes"},
                            {"title", "Test bench notes"},
                            {"text", "The load bank measures steady state current. "
                                     "Temperature sensors log the chamber profile. "
                                     "Battery discharge tests run overnight in the lab."}}));
    const auto out = engine.execute(
        makeReq("summarize_results", {{"query", "battery discharge tests"}, {"max_sentences", 2}}));
    REQUIRE(out.success);
    CHECK(out.result["hit_count"] == 1);
    REQUIRE(out.result["summary"].is_array());
    REQUIRE(out.result["summary"].size() >= 1);
    const auto& first = out.result["summary"][0];
    CHECK(first["doc_id"] == "notes");
    CHECK(first["sentence_index"].get<long long>() >= 0);
    CHECK(first["score"].get<double>() > 0.0);
    CHECK(first["sentence"].get<std::string>().find("Battery discharge") != std::string::npos);
}

TEST_CASE("research list, clear, and export behave truthfully") {
    ResearchEngine engine;
    engine.execute(makeReq("index_document", {{"title", "Alpha"}, {"text", "Alpha body content."}}));
    engine.execute(makeReq("index_document", {{"title", "Beta"}, {"text", "Beta body content."}}));

    const auto list = engine.execute(makeReq("list_documents", {}));
    REQUIRE(list.success);
    CHECK(list.result["document_count"] == 2);
    REQUIRE(list.result["documents"].size() == 2);

    const auto exportOut = engine.execute(makeReq("export_index", {}));
    REQUIRE(exportOut.success);
    CHECK(exportOut.result["document_count"] == 2);
    REQUIRE(exportOut.pendingArtifacts.size() == 1);
    const std::string exportPath = exportOut.pendingArtifacts[0].first;
    CHECK(std::filesystem::exists(exportPath));
    CHECK(std::filesystem::file_size(exportPath) > 0);
    std::filesystem::remove(exportPath);

    const auto cleared = engine.execute(makeReq("clear_index", {}));
    REQUIRE(cleared.success);
    CHECK(cleared.result["cleared"] == 2);

    const auto empty = engine.execute(makeReq("list_documents", {}));
    REQUIRE(empty.success);
    CHECK(empty.result["document_count"] == 0);
}
