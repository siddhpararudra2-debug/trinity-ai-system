# ♾️ Trinity AI System

[![Python 3.11+](https://img.shields.io/badge/Python-3.11%2B-blue.svg)](https://www.python.org/)
[![FastAPI](https://img.shields.io/badge/FastAPI-0.110%2B-00a393.svg)](https://fastapi.tiangolo.com/)
[![Next.js 16](https://img.shields.io/badge/Next.js-16-black.svg)](https://nextjs.org/)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

> **An LLM-Independent Engineering Operating System.**  
> Models reason and emit structured tool-calls, deterministic engines execute them, and independent checks validate the results.

Trinity AI is a modular, multi-platform engineering operating system designed to separate AI reasoning from deterministic execution. By acting as an orchestrator, Trinity ensures that Large Language Models (LLMs) only define *what* needs to be done via structured tool-calls, while highly reliable, domain-specific deterministic engines handle the *execution* and *validation*.

```text
API Request 
 └─> Structured Request 
      └─> Job Manager 
           └─> Engine Registry 
                └─> Domain Engine (Math, CAD, PCB, etc.)
                     └─> Validation 
                          └─> Artifact Manager 
                               └─> SQLite Lineage
```

## 🚀 Key Features

- **🧠 LLM-Independent Architecture**: Agnostic to the underlying model (OpenAI, Anthropic, Local). Models act solely as planners/reasoners.
- **⚙️ Deterministic Execution Engines**: Ships with robust built-in engines for diverse domains including Math, CAD Generation, PCB Design, Firmware, Vision, Simulation, Research, and Robotics.
- **🛡️ Strict Validation**: Output generation is strictly separated from validation and verification states (`GENERATED` | `VALIDATED` | `VERIFIED` | `FAILED`).
- **💻 Cross-Platform Foundations**:
  - **Python Backend**: High-performance FastAPI server handling jobs, storage, and AI interactions.
  - **Web Frontend**: Modern, reactive Next.js 16 / React 19 web application.
  - **Native Windows App**: A blazing-fast C++20 / Qt 6 native application layer for high-performance desktop execution.

## 📂 Project Layout

```text
trinity-ai-system/
├── app/native/           # C++20 / Qt6 Native Windows Application
├── config/               # System configurations (YAML)
├── data/                 # Caches, embeddings, and evaluation sets
├── docs/                 # Architectural documentation
├── frontend/             # Next.js 16 Web Application
├── notebooks/            # Jupyter notebooks for prompt experimentation
├── src/                  # Python FastAPI Backend
│   ├── agents/           # Domain-specific AI agents
│   ├── api/              # FastAPI endpoints
│   ├── engines/          # Deterministic execution engines
│   ├── llm/              # Model provider clients
│   └── ...               
└── tests/                # Comprehensive Python test suites
```

## 🛠️ Getting Started

### 1. Python Backend

The backend is built with FastAPI and requires Python 3.11+.

```bash
# Install the package in editable mode
python -m pip install -e .

# Run the FastAPI server (dev mode)
uvicorn src.main:app --reload
```

The API will be available at `http://localhost:8000/api`.

### 2. Next.js Frontend

The frontend is a modern web application built with Next.js 16. It communicates with the backend via the `NEXT_PUBLIC_TRINITY_API_URL` environment variable.

```bash
# Install dependencies
npm --prefix frontend install

# Start the development server
npm --prefix frontend run dev
```

The UI will be available at `http://localhost:3000`.

### 3. Native Windows App (C++ / Qt 6)

Trinity AI includes a high-performance native host built with C++20, CMake, and Qt 6. See [`app/native/README.md`](app/native/README.md) for detailed architecture, testing, and build instructions.

```powershell
cd app/native
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.3\msvc2022_64"
cmake --preset windows-debug
cmake --build --preset windows-debug
.\build\debug\Debug\Trinity.exe
```

## 📡 API Reference

The backend provides a comprehensive REST API for interacting with the engines and managing jobs.

- **System**: `GET /api/health`, `GET /api/engines`
- **Execution**: `POST /api/execute`, `POST /api/math/solve`, `POST /api/cad/generate`, `POST /api/requirements/execute`
- **State**: `GET /api/jobs`, `GET /api/jobs/{job_id}`, `GET /api/artifacts/{artifact_id}`

### Example Request (CAD Generation)

```json
POST /api/cad/generate
{
  "type": "quadcopter_frame",
  "parameters": {
    "overall_size": 50
  },
  "outputs": ["stl", "glb", "json"]
}
```

Every engine operation securely returns a standardized envelope:
`{ success, engine, operation, result, artifacts, validation, errors, job_id }`

## 📖 Documentation

For a deeper dive into the system's design patterns and capabilities, please refer to the [Architecture Documentation](docs/ARCHITECTURE.md).
