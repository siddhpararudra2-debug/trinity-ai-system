#include "trinity/engines/ResearchEngine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

#include "trinity/core/Logger.hpp"
#include "trinity/core/Uuid.hpp"

namespace trinity::engines {
namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxDocuments = 10000;
constexpr size_t kMaxTitleChars = 500;
constexpr size_t kMaxTextChars = 200000;

const std::set<std::string>& stopwords() {
    static const std::set<std::string> words = {
        "the", "a",   "an",  "and", "or",  "of",  "to",  "in",  "on",  "for", "with",
        "is",  "are", "was", "were", "be", "by",  "at",  "as",  "it",  "its", "this",
        "that", "from", "into", "per", "via", "not", "no",  "we",  "you", "they"};
    return words;
}

std::string toLowerCopy(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    const std::string lowered = toLowerCopy(text);
    for (const char c : lowered) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current.push_back(c);
        } else {
            if (current.size() >= 2 && stopwords().count(current) == 0) {
                tokens.push_back(current);
            }
            current.clear();
        }
    }
    if (current.size() >= 2 && stopwords().count(current) == 0) {
        tokens.push_back(current);
    }
    return tokens;
}

double round4(double value) { return std::round(value * 10000.0) / 10000.0; }

std::string requiredString(const core::Json& params, const std::string& key,
                           const std::string& operation) {
    if (!params.contains(key) || !params[key].is_string()) {
        throw core::RequestValidationError(
            "Operation '" + operation + "' requires string '" + key + "'",
            {{"operation", operation}, {"param", key}}, "engines");
    }
    std::string value = params[key].get<std::string>();
    if (value.empty()) {
        throw core::RequestValidationError("Parameter '" + key + "' must not be empty",
                                           {{"param", key}}, "engines");
    }
    return value;
}

int boundedInt(const core::Json& params, const std::string& key, int fallback, int lo, int hi) {
    if (!params.contains(key) || params[key].is_null()) {
        return fallback;
    }
    if (!params[key].is_number()) {
        throw core::RequestValidationError("Parameter '" + key + "' must be numeric",
                                           {{"param", key}}, "engines");
    }
    const double raw = params[key].get<double>();
    if (!std::isfinite(raw) || raw != std::floor(raw)) {
        throw core::RequestValidationError("Parameter '" + key + "' must be a whole number",
                                           {{"param", key}}, "engines");
    }
    const int value = static_cast<int>(raw);
    if (value < lo || value > hi) {
        throw core::RequestValidationError(
            "Parameter '" + key + "' must be within [" + std::to_string(lo) + ", " +
                std::to_string(hi) + "]",
            {{"param", key}, {"value", value}}, "engines");
    }
    return value;
}

std::vector<std::string> splitSentences(const std::string& text) {
    std::vector<std::string> sentences;
    std::string current;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        current.push_back(c);
        const bool boundary =
            (c == '.' || c == '!' || c == '?' || c == '\n') &&
            (i + 1 >= text.size() || std::isspace(static_cast<unsigned char>(text[i + 1])));
        if (boundary) {
            std::string trimmed;
            for (const char ch : current) {
                if (!std::isspace(static_cast<unsigned char>(ch))) {
                    trimmed.push_back(ch);
                } else if (trimmed.empty()) {
                    continue;
                } else {
                    trimmed.push_back(' ');
                }
            }
            while (!trimmed.empty() && trimmed.back() == ' ') {
                trimmed.pop_back();
            }
            if (trimmed.size() >= 15) {
                sentences.push_back(trimmed);
            }
            current.clear();
        }
    }
    if (!current.empty()) {
        std::string trimmed;
        for (const char ch : current) {
            if (!std::isspace(static_cast<unsigned char>(ch))) {
                trimmed.push_back(ch);
            } else if (!trimmed.empty()) {
                trimmed.push_back(' ');
            }
        }
        while (!trimmed.empty() && trimmed.back() == ' ') {
            trimmed.pop_back();
        }
        if (trimmed.size() >= 15) {
            sentences.push_back(trimmed);
        }
    }
    return sentences;
}

std::string makeSnippet(const std::string& text, const std::vector<std::string>& matched) {
    std::string lowered = toLowerCopy(text);
    size_t earliest = std::string::npos;
    for (const std::string& term : matched) {
        const size_t pos = lowered.find(term);
        if (pos != std::string::npos && (earliest == std::string::npos || pos < earliest)) {
            earliest = pos;
        }
    }
    if (earliest == std::string::npos) {
        const size_t len = std::min<size_t>(text.size(), 100);
        return text.substr(0, len) + (text.size() > len ? "..." : "");
    }
    const size_t start = earliest > 30 ? earliest - 30 : 0;
    const size_t len = 120;
    std::string snippet = (start > 0 ? "..." : "") +
                          text.substr(start, len) +
                          (start + len < text.size() ? "..." : "");
    return snippet;
}

}  // namespace

ResearchEngine::ResearchEngine() {
    name_ = "research";
    version_ = "0.1.0";
    capabilities_ = {"describe",  "index_document", "search",       "summarize_results",
                     "list_documents", "clear_index", "export_index"};
}

EngineResult ResearchEngine::executeIndex(const EngineRequest& request) {
    const std::string title = requiredString(request.parameters, "title", "index_document");
    const std::string text = requiredString(request.parameters, "text", "index_document");
    if (title.size() > kMaxTitleChars) {
        throw core::RequestValidationError("title exceeds " + std::to_string(kMaxTitleChars) +
                                               " characters",
                                           {{"param", "title"}}, "engines");
    }
    if (text.size() > kMaxTextChars) {
        throw core::RequestValidationError("text exceeds " + std::to_string(kMaxTextChars) +
                                               " characters",
                                           {{"param", "text"}}, "engines");
    }
    if (documents_.size() >= kMaxDocuments) {
        throw core::RequestValidationError("Index is full (max " +
                                               std::to_string(kMaxDocuments) + " documents)",
                                           {{"limit", kMaxDocuments}}, "engines");
    }
    Document doc;
    doc.title = title;
    doc.text = text;
    doc.source = request.parameters.value("source", "");
    doc.id = request.parameters.value("doc_id", "");
    if (doc.id.empty()) {
        doc.id = core::newUuid();
    }
    const bool duplicate =
        std::any_of(documents_.begin(), documents_.end(),
                    [&doc](const Document& d) { return d.id == doc.id; });
    if (duplicate) {
        throw core::RequestValidationError("doc_id '" + doc.id + "' already exists",
                                           {{"doc_id", doc.id}}, "engines");
    }
    doc.titleTokens = tokenize(doc.title);
    doc.bodyTokens = tokenize(doc.text);
    documents_.push_back(std::move(doc));
    const Document& stored = documents_.back();

    core::Json data{{"doc_id", stored.id},
                    {"title", stored.title},
                    {"source", stored.source},
                    {"token_count", static_cast<long long>(stored.bodyTokens.size())},
                    {"document_count", static_cast<long long>(documents_.size())}};
    EngineResult out = successResult(request, data);
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "research document indexed",
        core::Json{{"doc_id", stored.id},
                   {"documents", static_cast<long long>(documents_.size())}});
    return out;
}

core::Json ResearchEngine::searchHits(const std::string& query, int limit) const {
    const std::vector<std::string> queryTokens = tokenize(query);
    std::vector<std::string> terms;
    for (const std::string& token : queryTokens) {
        if (std::find(terms.begin(), terms.end(), token) == terms.end()) {
            terms.push_back(token);
        }
    }
    core::Json hits = core::Json::array();
    if (terms.empty()) {
        core::Json out{{"query", query},
                       {"query_terms", core::Json::array()},
                       {"total_documents", static_cast<long long>(documents_.size())},
                       {"hit_count", 0},
                       {"hits", hits}};
        return out;
    }

    struct Scored {
        const Document* doc;
        double score;
        int matched;
        int occurrences;
        std::vector<std::string> matchedTerms;
    };
    std::vector<Scored> scored;
    for (const Document& doc : documents_) {
        std::map<std::string, int> freq;
        for (const std::string& token : doc.bodyTokens) {
            ++freq[token];
        }
        std::set<std::string> titleSet(doc.titleTokens.begin(), doc.titleTokens.end());
        Scored entry;
        entry.doc = &doc;
        entry.matched = 0;
        entry.occurrences = 0;
        int matchedInTitle = 0;
        for (const std::string& term : terms) {
            const auto it = freq.find(term);
            const int bodyCount = it == freq.end() ? 0 : it->second;
            const int titleCount = titleSet.count(term);
            if (bodyCount > 0 || titleCount > 0) {
                ++entry.matched;
                entry.occurrences += bodyCount + titleCount;
                entry.matchedTerms.push_back(term);
                if (titleCount > 0) {
                    ++matchedInTitle;
                }
            }
        }
        if (entry.matched == 0) {
            continue;
        }
        entry.score = round4(static_cast<double>(entry.matched) /
                                 static_cast<double>(terms.size()) +
                             0.25 * static_cast<double>(matchedInTitle) /
                                 static_cast<double>(terms.size()));
        scored.push_back(std::move(entry));
    }

    std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.occurrences != b.occurrences) {
            return a.occurrences > b.occurrences;
        }
        return a.doc->id < b.doc->id;
    });

    int rank = 0;
    for (const Scored& entry : scored) {
        if (rank >= limit) {
            break;
        }
        ++rank;
        core::Json matchedArray = core::Json::array();
        for (const std::string& term : entry.matchedTerms) {
            matchedArray.push_back(term);
        }
        hits.push_back(core::Json{{"rank", rank},
                                   {"doc_id", entry.doc->id},
                                   {"title", entry.doc->title},
                                   {"source", entry.doc->source},
                                   {"score", entry.score},
                                   {"matched_terms", matchedArray},
                                   {"occurrences", entry.occurrences},
                                   {"snippet", makeSnippet(entry.doc->text, entry.matchedTerms)}});
    }

    core::Json out{{"query", query},
                   {"query_terms", [&terms]() {
                        core::Json arr = core::Json::array();
                        for (const std::string& t : terms) {
                            arr.push_back(t);
                        }
                        return arr;
                    }()},
                   {"total_documents", static_cast<long long>(documents_.size())},
                   {"hit_count", rank},
                   {"hits", hits}};
    return out;
}

EngineResult ResearchEngine::executeSearch(const EngineRequest& request) {
    const std::string query = requiredString(request.parameters, "query", "search");
    const int limit = boundedInt(request.parameters, "limit", 5, 1, 50);
    core::Json data = searchHits(query, limit);
    EngineResult out = successResult(request, data);
    out.metadata["research"] = {{"hit_count", data.value("hit_count", 0)},
                                {"total_documents", data.value("total_documents", 0LL)}};
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "research search",
        core::Json{{"hit_count", data.value("hit_count", 0)},
                   {"total_documents", data.value("total_documents", 0LL)}});
    return out;
}

EngineResult ResearchEngine::executeSummarize(const EngineRequest& request) {
    const std::string query = requiredString(request.parameters, "query", "summarize_results");
    const int hitLimit = boundedInt(request.parameters, "limit", 5, 1, 50);
    const int maxSentences = boundedInt(request.parameters, "max_sentences", 3, 1, 20);

    const core::Json search = searchHits(query, hitLimit);
    const std::vector<std::string> queryTerms = tokenize(query);

    struct Candidate {
        int hitRank;
        size_t docIndex;
        size_t sentenceIndex;
        std::string sentence;
        double score;
    };
    std::vector<Candidate> candidates;
    const core::Json& hits = search["hits"];
    for (size_t h = 0; h < hits.size(); ++h) {
        const std::string docId = hits[h].value("doc_id", "");
        const auto docIt = std::find_if(documents_.begin(), documents_.end(),
                                        [&docId](const Document& d) { return d.id == docId; });
        if (docIt == documents_.end()) {
            continue;
        }
        const std::vector<std::string> sentences = splitSentences(docIt->text);
        for (size_t s = 0; s < sentences.size(); ++s) {
            const std::vector<std::string> sentenceTokens = tokenize(sentences[s]);
            if (sentenceTokens.empty()) {
                continue;
            }
            std::set<std::string> unique(sentenceTokens.begin(), sentenceTokens.end());
            int overlap = 0;
            for (const std::string& term : queryTerms) {
                if (unique.count(term) > 0) {
                    ++overlap;
                }
            }
            if (overlap == 0) {
                continue;
            }
            Candidate candidate;
            candidate.hitRank = hits[h].value("rank", 0);
            candidate.docIndex = static_cast<size_t>(
                docIt - documents_.begin());
            candidate.sentenceIndex = s;
            candidate.sentence = sentences[s];
            candidate.score =
                round4(static_cast<double>(overlap) / static_cast<double>(sentenceTokens.size()));
            candidates.push_back(std::move(candidate));
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) {
                         if (a.score != b.score) {
                             return a.score > b.score;
                         }
                         if (a.hitRank != b.hitRank) {
                             return a.hitRank < b.hitRank;
                         }
                         return a.sentenceIndex < b.sentenceIndex;
                     });

    core::Json summary = core::Json::array();
    const size_t take = std::min<size_t>(static_cast<size_t>(maxSentences), candidates.size());
    for (size_t i = 0; i < take; ++i) {
        const Candidate& candidate = candidates[i];
        const Document& doc = documents_[candidate.docIndex];
        summary.push_back(core::Json{{"doc_id", doc.id},
                                     {"title", doc.title},
                                     {"hit_rank", candidate.hitRank},
                                     {"sentence_index", static_cast<long long>(
                                                            candidate.sentenceIndex)},
                                     {"score", candidate.score},
                                     {"sentence", candidate.sentence}});
    }

    core::Json data{{"query", query},
                    {"query_terms", [&queryTerms]() {
                        core::Json arr = core::Json::array();
                        for (const std::string& t : queryTerms) {
                            arr.push_back(t);
                        }
                        return arr;
                    }()},
                    {"total_documents", static_cast<long long>(documents_.size())},
                    {"hit_count", search.value("hit_count", 0)},
                    {"sentences_taken", static_cast<long long>(take)},
                    {"summary", summary}};
    EngineResult out = successResult(request, data);
    out.metadata["research"] = {{"sentences_taken", static_cast<long long>(take)},
                                {"hit_count", search.value("hit_count", 0)}};
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "research summarize",
        core::Json{{"sentences_taken", static_cast<long long>(take)},
                   {"hit_count", search.value("hit_count", 0)}});
    return out;
}

EngineResult ResearchEngine::executeList(const EngineRequest& request) {
    core::Json docs = core::Json::array();
    for (const Document& doc : documents_) {
        docs.push_back(core::Json{{"doc_id", doc.id},
                                  {"title", doc.title},
                                  {"source", doc.source},
                                  {"token_count",
                                   static_cast<long long>(doc.bodyTokens.size())}});
    }
    EngineResult out =
        successResult(request, core::Json{{"documents", docs},
                                          {"document_count",
                                           static_cast<long long>(documents_.size())}});
    out.validation = validate(out);
    return out;
}

EngineResult ResearchEngine::executeClear(const EngineRequest& request) {
    const long long cleared = static_cast<long long>(documents_.size());
    documents_.clear();
    EngineResult out = successResult(request, core::Json{{"cleared", cleared}});
    out.validation = validate(out);
    core::Logger::instance().info("engines", "research index cleared",
                                  core::Json{{"cleared", cleared}});
    return out;
}

EngineResult ResearchEngine::executeExport(const EngineRequest& request) {
    std::error_code ec;
    const fs::path workDir = fs::temp_directory_path(ec) / "trinity_research";
    fs::create_directories(workDir, ec);
    if (ec) {
        throw core::EngineExecutionError("Cannot create research scratch directory: " +
                                             ec.message(),
                                         {}, "engines");
    }
    std::string id = core::newUuid();
    id.erase(std::remove(id.begin(), id.end(), '-'), id.end());
    const fs::path outPath = workDir / ("index_" + id.substr(0, 8) + ".jsonl");
    std::ofstream stream(outPath);
    if (!stream) {
        throw core::EngineExecutionError("Cannot open research index file for writing", {},
                                         "engines");
    }
    for (const Document& doc : documents_) {
        const core::Json line = core::Json{{"id", doc.id},
                                           {"title", doc.title},
                                           {"source", doc.source},
                                           {"text", doc.text}};
        stream << line.dump() << "\n";
    }
    stream.close();
    if (!stream) {
        throw core::EngineExecutionError("Failed while writing research index", {},
                                         "engines");
    }
    EngineResult out = successResult(
        request, core::Json{{"document_count", static_cast<long long>(documents_.size())},
                            {"path", outPath.string()}});
    out.pendingArtifacts.emplace_back(outPath.string(), "jsonl");
    out.validation = validate(out);
    return out;
}

EngineResult ResearchEngine::execute(const EngineRequest& request) {
    if (request.operation == "describe") {
        EngineResult out =
            successResult(request, {{"engine", name_},
                                    {"version", version_},
                                    {"capabilities", capabilities_},
                                    {"index", core::Json{{"documents",
                                                          static_cast<long long>(
                                                              documents_.size())},
                                                         {"max_documents",
                                                          static_cast<long long>(
                                                              kMaxDocuments)}}},
                                    {"ranking", "deterministic token overlap with title boost"},
                                    {"limits",
                                     {{"max_title_chars",
                                       static_cast<long long>(kMaxTitleChars)},
                                      {"max_text_chars", static_cast<long long>(kMaxTextChars)}}}});
        out.validation = validate(out);
        return out;
    }
    try {
        if (request.operation == "index_document") {
            return executeIndex(request);
        }
        if (request.operation == "search") {
            return executeSearch(request);
        }
        if (request.operation == "summarize_results") {
            return executeSummarize(request);
        }
        if (request.operation == "list_documents") {
            return executeList(request);
        }
        if (request.operation == "clear_index") {
            return executeClear(request);
        }
        if (request.operation == "export_index") {
            return executeExport(request);
        }
        return capabilityUnavailable(
            request, "Research operation '" + request.operation + "' is not implemented yet");
    } catch (const core::TrinityError& exc) {
        EngineResult out = failureResult(request, exc.what(),
                                         exc.toJson().value("details", core::Json::object()));
        out.errors.clear();
        out.addError(exc.info());
        out.validation = validate(out);
        return out;
    }
}

validation::ValidationResult ResearchEngine::validate(const EngineResult& result) const {
    validation::ValidationResult validation;
    validation.operation = result.operation;
    validation.jobId = result.jobId;
    validation.checks = {{"engine", "research"}, {"operation", result.operation}};
    if (!result.success) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "Research operation failed";
        validation::ValidationMessage msg;
        msg.rule = "research.success";
        msg.severity = validation::Severity::Error;
        msg.passed = false;
        msg.message = "Engine reported failure";
        validation.addMessage(std::move(msg));
        if (!result.errors.empty()) {
            validation.error = result.errors.front();
        }
        return validation;
    }
    const bool metadataOp = result.operation == "describe" || result.operation == "index_document" ||
                            result.operation == "list_documents" ||
                            result.operation == "clear_index" || result.operation == "export_index";
    validation.status = validation::ValidationStatus::Validated;
    validation.message = metadataOp ? "Research metadata operation completed"
                                    : "Research completed with deterministic local ranking";
    validation::ValidationMessage msg;
    msg.rule = metadataOp ? "research.metadata" : "research.completed";
    msg.severity = validation::Severity::Info;
    msg.passed = true;
    msg.message = validation.message;
    validation.addMessage(std::move(msg));
    return validation;
}

}  // namespace trinity::engines
