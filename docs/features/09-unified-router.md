# Unified Router / Orchestrator

**Primary technology:** Hybrid keyword/regex + optional LLM  
**Primary output:** Engine selection and routing  
**Domain:** System routing

## 1. Feature Definition

Unified Router / Orchestrator is a Trinity AI capability defined at the product/architecture level. See the Trinity roadmap for authoritative scope.

## 2. Purpose and Problem

Reduce fragmentation between domain expertise, computation, generation, and verification within Trinity engineering, scientific, and research workflows.

## 3. Inputs

- Natural-language user request
- Session/context metadata

## 4. Outputs

- Engine-selection decision
- Routing result
- Downstream execution result

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

app/unified_router/orchestrator.py

## 11. Documentation Status

Product/architecture specification derived from the Trinity roadmap. Repository code is the secondary reference for boundaries and terminology.
