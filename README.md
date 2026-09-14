# Trinity AI

An LLM-independent engineering operating system designed for deterministic execution, independent checks, and robust verification. 

Trinity AI splits reasoning and execution: large language models (LLMs) may perform reasoning at a higher level, while a rigid suite of deterministic tools handles the execution and mathematically verifiable checks validate the outputs.

## 🚀 Features

* **LLM-Independent Execution**: Executes engineering tasks reliably without relying on probabilistic model outputs during the critical execution phase.
* **Deterministic Tools**: Uses structured requests, job managers, and engine registries for predictable results.
* **Independent Verification**: Incorporates validation checks separated from the generation step to ensure integrity.
* **Built-in CAD Engine**: Utilizes a library-independent IR, deterministic native mesh builder, and outputs to binary STL/GLB. 
* **Full-Stack Application**: Comes with a Next.js 16/React 19 Frontend and a Python Backend.

## 🏗️ Architecture Overview

The system architecture flows as follows:

`API → structured request → job manager → engine registry → engine → validation → artifact manager → SQLite lineage`

A model provider can emit structured tool calls but will never execute engines itself, maintaining strict bounds between logic and engineering execution.

For detailed information, refer to [ARCHITECTURE.md](docs/ARCHITECTURE.md).

## 💻 Tech Stack

* **Frontend**: Next.js 16, React 19, TypeScript
* **Backend**: Python, Uvicorn
* **Storage/Lineage**: SQLite Artifact Manager
* **CAD Processing**: Custom engine with deterministic mesh building

## 🛠️ Getting Started

### Prerequisites

* Node.js
* Python 3.9+
* npm or yarn

### Running the Backend

The backend is built with Python. To start the local server:

```bash
cd backend
python -m pip install -e .
uvicorn app.main:app --reload
```
The backend API should now be running locally.

### Running the Frontend

The frontend is a modern Next.js application.

```bash
# In the project root
npm install
npm run dev
```
The frontend should be accessible at `http://localhost:3000`.

## 🌐 API Endpoints

The API is structured to handle execution, artifact management, and specialized tasks like math solving and CAD generation.

**Core Endpoints:**
* `GET /api/health` - Check system health
* `GET /api/engines` - List available deterministic engines
* `POST /api/execute` - Execute a generic job
* `POST /api/math/solve` - Solve a mathematical equation/problem
* `POST /api/cad/generate` - Generate a CAD model

**Job & Artifact Management:**
* `GET /api/jobs` - List all jobs
* `GET /api/jobs/{job_id}` - Retrieve details of a specific job
* `GET /api/artifacts/{artifact_id}` - Retrieve a generated artifact

**Flagship Example (CAD Generation):**
```json
POST /api/cad/generate

{
  "parameters": {
    "overall_size": 50
  },
  "outputs": ["stl", "glb", "json"]
}
```
This generates a validated 50 mm quadcopter frame and returns persisted, checksummed artifacts. 
*(Note: STEP output is currently restricted until CadQuery/OpenCascade is fully configured.)*

For more comprehensive API details, see [API Reference](docs/API.md).

## 📖 Documentation

More comprehensive guides and documentation can be found in the `docs/` directory:
* [Architecture](docs/ARCHITECTURE.md)
* [API Reference](docs/API.md)
* [CAD Details](docs/CAD.md)
