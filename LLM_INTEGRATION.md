# LLM Integration Documentation (Item 8)

**Adapter**: `src.llm.adapters.LLMAdapter`
**Base URL**: `LLM_BASE_URL` from `.env.dummy` (e.g. `http://localhost:11434/v1/chat/completions`)
**Model**: `LLM_MODEL_NAME` from `.env.dummy` (e.g. `local-model`)

---

## Overview

This module provides a single adapter that routes **all** LLM calls and marks every
response with the `LLM-LINK:` identifier. The adapter can operate in two modes:

| Mode | Env Var | Behavior |
|------|---------|----------|
| **Mock** | `LLM_MOCK=1` | Returns canned JSON responses without network calls. Useful for development and testing. |
| **Real** | (default) | Routes through `LocalClient` to an OpenAI-compatible endpoint. |

Every LLM-generated string that passes through this adapter is prefixed/identified
with the `LLM-LINK:` marker, allowing the system to distinguish LLM output from
other sources.

---

## API Reference

### `LLMAdapter.chat_completion(messages, **kwargs)`

Send a chat completion request through the adapter.

**Args:**
- `messages`: `List[dict[str, str]]` — List of message dicts with `'role'` and `'content'`.
- `**kwargs`: Additional parameters (e.g. `temperature`, `top_p`, `max_tokens`).

**Returns:**
```json
{
  "content": "LLM-LINK: <generated text>",
  "model": "<model_name>"
}
```

**Example:**
```python
from src.llm import chat

response = chat([
    {"role": "system", "content": "You are Trinity AI."},
    {"role": "user", "content": "Design a quadcopter frame."},
])
# Returns: {"content": "LLM-LINK: I can help you design a quadcopter frame...", "model": "local-model"}
```

---

### `LLMAdapter.chat_completion_stream(messages, **kwargs)`

Stream a chat completion response.

**Yields:**
- Chunks of text marked with `LLM-LINK:`.

**Example:**
```python
for chunk in chat_stream([{"role": "user", "content": "Tell me about Trinity"}]):
    print(chunk, end="")
```

---

### `llm_link_mark(text)` / `unlink_mark(text)`

Utility functions to add/remove the `LLM-LINK:` marker.

- `llm_link_mark("hello")` → `"LLM-LINK: hello"`
- `unlink_mark("LLM-LINK: hello")` → `"hello"`

---

## Integration Points

All LLM-mediated operations in Trinity should go through the adapter. Below is
the complete list of integration points, their input formats, and expected output
formats.

### 1. Design Planning

**Purpose**: Generate a multi-step plan for CAD → PCB → firmware → verification.

**Input**:
```json
{
  "messages": [
    {"role": "system", "content": "You are Trinity AI, a full-stack EDA assistant."},
    {"role": "user", "content": "I want to design an enclosure with a lid for a 50mm x 40mm PCB."}
  ]
}
```

**Output**:
```json
{
  "content": "LLM-LINK: I'll help you design an enclosure with a lid...",
  "model": "local-model"
}
```

---

### 2. CAD Operation

**Purpose**: Request a CAD generation or operation.

**Input**:
```json
{
  "messages": [
    {"role": "user", "content": "Generate a quadcopter frame with 4 arms."}
  ]
}
```

**Output**:
```json
{
  "content": "LLM-LINK: I'll generate a quadcopter frame with 4 arms...",
  "model": "local-model"
}
```

---

### 3. PCB Operation

**Purpose**: Request a PCB generation or verification.

**Input**:
```json
{
  "messages": [
    {"role": "user", "content": "Create a 50mm x 40mm board with ESP32 and capacitors."}
  ]
}
```

**Output**:
```json
{
  "content": "LLM-LINK: I'll create a 50mm x 40mm board with ESP32...",
  "model": "local-model"
}
```

---

### 4. Firmware Generation

**Purpose**: Generate firmware source code for the selected MCU.

**Input**:
```json
{
  "messages": [
    {"role": "user", "content": "Generate firmware for ESP32-WROOM-32 with UART and SPI."}
  ]
}
```

**Output**:
```json
{
  "content": "LLM-LINK: I'll generate firmware for ESP32-WROOM-32 with UART and SPI...",
  "model": "local-model"
}
```

---

### 5. Verification Report

**Purpose**: Request a verification/unverified report for a design.

**Input**:
```json
{
  "messages": [
    {"role": "user", "content": "Verify this design and report any issues."}
  ]
}
```

**Output**:
```json
{
  "content": "LLM-LINK: Based on the datasheets available...",
  "model": "local-model"
}
```

---

### 6. Export Guidance

**Purpose**: Ask which export formats are available and how to use them.

**Input**:
```json
{
  "messages": [
    {"role": "user", "content": "How do I export Gerber files and BOM CSV?"}
  ]
}
```

**Output**:
```json
{
  "content": "LLM-LINK: Trinity can export STL/STEP for CAD, Gerber for PCB, BOM CSV for purchasing, and firmware source code for compilation.",
  "model": "local-model"
}
```

---

### 7. Literature RAG Query

**Purpose**: Query the literature RAG pipeline for design information.

**Input**:
```json
{
  "messages": [
    {"role": "user", "content": "What does Trinity use for local embeddings?"}
  ]
}
```

**Output**:
```json
{
  "content": "LLM-LINK: Trinity uses a hash-based embedding model (controlled by EMBEDDING_MODEL in .env.dummy) with a local vector store. The same text always maps to the same vector.",
  "model": "local-model"
}
```

---

## Mock Mode (Development & Testing)

Set `LLM_MOCK=1` in `.env.dummy` to enable mock mode. In this mode, the adapter
returns canned responses based on keyword matching in the user message. No network
calls are made.

**To enable**: `LLM_MOCK=1`

**Canned responses** are triggered by keywords in the user message:
- `plan` / `design` → Design planning response
- `cad` → CAD operation response
- `pcb` / `board` → PCB operation response
- `firmware` / `code` → Firmware generation response
- `verify` / `check` → Verification report response
- `export` / `download` / `stl` / `gerber` → Export guidance response
- `bom` / `bill` / `parts` → BOM generation response
- `question` / `info` → General information response

---

## Configuration (.env.dummy)

| Variable | Default | Description |
|----------|---------|-------------|
| `LLM_BASE_URL` | `http://localhost:11434/v1/chat/completions` | OpenAI-compatible endpoint URL |
| `LLM_MODEL_NAME` | `local-model` | Model ID exposed by the server |
| `LLM_MOCK` | `0` | `1` to enable mock mode, `0` for real endpoint |
| `TRINITY_MODEL_ENDPOINT` | `` | Overrides `LLM_BASE_URL` for LocalClient |
| `TRINITY_MODEL_NAME` | `` | Overrides `LLM_MODEL_NAME` for LocalClient |
| `TRINITY_MODEL_API_KEY` | `` | Optional API key for gateways that demand it |

---

## Migration Path: Real LLM → Mock

1. **Set `LLM_MOCK=1`** in `.env.dummy` to start with mock responses.
2. **Implement canned responses** for each integration point (already provided).
3. **Replace mock with real client** by setting `LLM_MOCK=0` and configuring `LLM_BASE_URL` / `LLM_MODEL_NAME`.
4. **Zero code changes** needed — the adapter automatically switches modes.

---

## Testing

```bash
# Enable mock mode
set LLM_MOCK=1

# Test the adapter
python -c "
from src.llm import chat
resp = chat([{'role': 'user', 'content': 'Design a PCB board'}])
print(resp['content'][:60])
# Should output: LLM-LINK: I'll create a...
"
```

---
*Document generated automatically. Update `LLM_INTEGRATION.md` whenever a new integration point is added or the adapter API changes.*