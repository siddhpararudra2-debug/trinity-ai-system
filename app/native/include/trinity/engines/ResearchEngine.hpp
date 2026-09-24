#pragma once

// Research engine: deterministic local index + token-overlap search +
// extractive summaries over documents the caller supplies. No web
// access, no embeddings, no LLM-generated prose — provenance only.

#include <string>
#include <vector>

#include "Engine.hpp"

namespace trinity::engines {

class ResearchEngine : public EngineBase {
public:
    ResearchEngine();

    EngineResult execute(const EngineRequest& request) override;
    validation::ValidationResult validate(const EngineResult& result) const override;

private:
    struct Document {
        std::string id;
        std::string title;
        std::string text;
        std::string source;
        std::vector<std::string> titleTokens;
        std::vector<std::string> bodyTokens;
    };

    EngineResult executeIndex(const EngineRequest& request);
    EngineResult executeSearch(const EngineRequest& request);
    EngineResult executeSummarize(const EngineRequest& request);
    EngineResult executeList(const EngineRequest& request);
    EngineResult executeClear(const EngineRequest& request);
    EngineResult executeExport(const EngineRequest& request);

    core::Json searchHits(const std::string& query, int limit) const;

    std::vector<Document> documents_;
};

}  // namespace trinity::engines
