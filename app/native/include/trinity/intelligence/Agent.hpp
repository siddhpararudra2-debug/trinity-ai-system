#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include "../core/Json.hpp"
#include "ModelRequest.hpp"
#include "ModelResponse.hpp"

namespace trinity::intelligence {

struct AgentOptions {
    int maxToolCalls = 10;
    int maxSteps = 15;
    int timeoutMs = 60000;
    int maxHistorySize = 50; // Truncate older messages to save context
};

class Agent {
public:
    Agent(const std::string& sessionId, const AgentOptions& options = AgentOptions());

    // Execute a request and run the agent loop
    ModelResponse execute(const std::string& userPrompt, std::atomic<bool>* cancelToken = nullptr);
    
    // Add a message to context
    void addMessage(const Message& msg);
    
    // Clear context
    void clear();

    const std::vector<Message>& getHistory() const { return _history; }
    
    // Truncate history if it exceeds limits
    void pruneHistory();

private:
    std::string _sessionId;
    AgentOptions _options;
    std::vector<Message> _history;
};

} // namespace trinity::intelligence
