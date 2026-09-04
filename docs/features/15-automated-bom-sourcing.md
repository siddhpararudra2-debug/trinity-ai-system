# Automated BOM and Sourcing

**Primary technology:** PCB engine + distributor integrations  
**Primary output:** Structured BOM with pricing/stock  
**Domain:** Procurement

## 1. Feature Definition

Automated BOM and Sourcing is a Trinity AI capability defined at the product/architecture level. See the Trinity roadmap for authoritative scope.

## 2. Purpose and Problem

Reduce fragmentation between domain expertise, computation, generation, and verification within Trinity engineering, scientific, and research workflows.

## 3. Inputs

- KiCad PCB/component data
- Sourcing preferences

## 4. Outputs

- Normalized BOM
- Part numbers
- Pricing and stock

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

app/sourcing/bom_service.py

## 11. Documentation Status

Product/architecture specification derived from the Trinity roadmap. Repository code is the secondary reference for boundaries and terminology.
