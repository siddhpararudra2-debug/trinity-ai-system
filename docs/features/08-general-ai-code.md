# General AI / Code

**Primary technology:** LLM  
**Primary output:** General assistance and code  
**Domain:** Fallback/orchestration

## 1. Feature Definition

General AI / Code is a Trinity AI capability defined at the product/architecture level. See the Trinity roadmap for authoritative scope.

## 2. Purpose and Problem

Reduce fragmentation between domain expertise, computation, generation, and verification within Trinity engineering, scientific, and research workflows.

## 3. Inputs

- General questions
- Coding tasks
- Ambiguous requests

## 4. Outputs

- Natural-language assistance
- Generated code
- Engine handoffs

## 5. Conceptual Workflow

1. Understand the request
2. Extract the domain task
3. Perform computation or generation
4. Validate the result
5. Format for presentation
6. Return to user or downstream workflow

## 6. Architecture Principles

- Keep deterministic domain engines separate from general LLM reasoning where practical
- Maintain clear boundaries between interpretation, computation, verification, and presentation
- Version stable contracts once externally relied upon

## 7. Interfaces and Contracts

Stable request, response, artifact, and error contracts are exposed via OpenAPI and feature routes under /api/.

## 8. Validation and Edge Cases

Representative invalid, ambiguous, unsupported, and resource-limit cases must be handled explicitly with structured errors.

## 9. Testing Strategy

Maintain a test matrix with happy-path, invalid, adversarial, boundary, and integration cases.

## 10. Implementation Reference

app/engines/general_ai.py

## 11. Documentation Status

Product/architecture specification derived from the Trinity roadmap. Repository code is the secondary reference for boundaries and terminology.
