#include "trinity/intelligence/Agent.hpp"
#include "trinity/intelligence/ModelManager.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/workflows/Workflow.hpp"
#include "trinity/intelligence/Planner.hpp"
#include <chrono>

namespace trinity::intelligence {

Agent::Agent(const std::string& sessionId, Planner* planner, const AgentOptions& options)
    : _sessionId(sessionId), _planner(planner), _options(options) {}

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
            if (_planner) {
                std::vector<ToolCall> calls = lastResponse.toolCalls;
                if (calls.empty() && lastResponse.hasToolCall) {
                    calls.push_back(lastResponse.toolCall);
                }
                
                PlanResult planRes = _planner->executeToolCalls(calls, request.requestId);
                
                // Add the tool results to history
                Message toolResultMsg;
                toolResultMsg.role = MessageRole::User; // Tool results usually come back as user/system
                toolResultMsg.content = planRes.toJson().dump();
                addMessage(toolResultMsg);
                
                // Don't break, let the loop continue to get the final response from the model
            } else {
                // No planner, we can't execute tools. Just break.
                break;
            }
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
