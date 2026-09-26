#include "trinity/intelligence/Agent.hpp"
#include "trinity/intelligence/ModelManager.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/workflows/Workflow.hpp"
#include <chrono>

namespace trinity::intelligence {

Agent::Agent(const std::string& sessionId, const AgentOptions& options)
    : _sessionId(sessionId), _options(options) {}

void Agent::addMessage(const Message& msg) {
    _history.push_back(msg);
    pruneHistory();
}

void Agent::clear() {
    _history.clear();
}

void Agent::pruneHistory() {
    // Keep system message if first, then prune oldest messages to fit maxHistorySize
    if (_history.size() <= static_cast<size_t>(_options.maxHistorySize)) return;
    
    std::vector<Message> pruned;
    size_t startIdx = 0;
    if (!_history.empty() && _history[0].role == MessageRole::System) {
        pruned.push_back(_history[0]);
        startIdx = 1;
    }
    
    size_t keepCount = _options.maxHistorySize - pruned.size();
    if (keepCount > 0 && _history.size() - startIdx > keepCount) {
        startIdx = _history.size() - keepCount;
    }
    
    for (size_t i = startIdx; i < _history.size(); ++i) {
        pruned.push_back(_history[i]);
    }
    _history = std::move(pruned);
}

ModelResponse Agent::execute(const std::string& userPrompt, std::atomic<bool>* cancelToken) {
    addMessage({MessageRole::User, userPrompt});

    ModelRequest request;
    request.requestId = core::newUuid();
    
    int stepCount = 0;
    auto startTime = std::chrono::steady_clock::now();
    ModelResponse lastResponse;
    
    while (stepCount < _options.maxSteps) {
        if (cancelToken && cancelToken->load()) {
            lastResponse.success = false;
            lastResponse.error = core::Json{{"error", "Cancelled"}};
            break;
        }
        
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count() > _options.timeoutMs) {
            lastResponse.success = false;
            lastResponse.error = core::Json{{"error", "Timeout exceeded"}};
            break;
        }
        
        request.messages = _history;
        lastResponse = ModelManager::instance().generate(request);
        
        if (!lastResponse.success) {
            break;
        }
        
        // Record assistant response
        Message assistantMsg;
        assistantMsg.role = MessageRole::Assistant;
        assistantMsg.content = lastResponse.text;
        if (lastResponse.hasToolCall || !lastResponse.toolCalls.empty()) {
            assistantMsg.content = lastResponse.toJson().dump(); // record the tool call intent
        }
        addMessage(assistantMsg);
        
        if (lastResponse.hasToolCall || !lastResponse.toolCalls.empty()) {
            // Need to execute the tool(s)! Wait, Agent should execute it?
            // "The LLM must never have direct access to... The architecture must remain: LLM -> Structured ToolCall -> Tool Schema Validation -> Permission Check -> Trinity Engine -> Validation -> ToolResult -> LLM"
            
            // For now, if the LLM emits a tool call, we execute it in the loop or planner.
            // But wait, the prompt says Planner -> WorkflowExecutor. Let's delegate execution to the Planner/WorkflowExecutor in the higher layer, or just here.
            // Let's break to let the caller handle the ToolCall or we execute it here.
            // I'll execute it here to close the agent loop.
            break; // We'll implement actual tool execution here soon.
        } else {
            // No tool call, final response reached
            break;
        }
        
        stepCount++;
    }
    
    if (stepCount >= _options.maxSteps) {
        lastResponse.success = false;
        lastResponse.error = core::Json{{"error", "Max steps exceeded"}};
    }
    
    return lastResponse;
}

} // namespace trinity::intelligence
