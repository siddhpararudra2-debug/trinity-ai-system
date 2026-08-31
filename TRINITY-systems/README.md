# Trinity AI

**Trinity AI** is a unified AI Engineering and Research Operating System. It provides a single terminal-style chat interface to interact with 8 specialized engines, bridging complex engineering, research, and design tools into a unified workspace.

## Engines

1. **Math Engine (SymPy)**: Solves, integrates, differentiates, simplifies, and factors mathematical expressions, rendering the output in LaTeX format.
2. **Quantum Lab**: Simulates quantum circuits (using NumPy for Bloch sphere and circuit templates like bell, hadamard, grover, etc.) and shows histograms of measurement counts.
3. **Maker CAD**: Generates Fusion 360 Python API scripts for 3D modeling and provides download links.
4. **Maker PCB**: Generates KiCad 7 S-expression schematics and layouts for printed circuit boards.
5. **Literature RAG**: Interfaces with the real arXiv Atom API to search for and render academic paper cards with links.
6. **Vision Engine**: Handles image uploads with optional pix2tex for handwritten LaTeX OCR and Tesseract for printed-text fallback.
7. **Firmware Engine**: Generates target-specific microcontroller and flight-controller projects (supporting ESP32, Arduino, STM32, Raspberry Pi Pico, Pixhawk, etc.).
8. **General AI / Code**: A fallback engine for unrecognized queries.

## Tech Stack

* **Frontend:** Built with React + Vite, Tailwind CSS v4, TanStack React Query, and uses KaTeX and Recharts for rendering math and quantum histograms. 
* **Backend:** Powered by Python 3.11 + FastAPI + Uvicorn, using SQLite (via aiosqlite) and SQLAlchemy for async database operations.
* **Architecture:** Uses keyword/regex-based routing to automatically send user messages to the appropriate specialized engine without necessarily relying on an LLM, though it has optional LLM integration.

For more technical details and setup instructions, please refer to the `replit.md` file.
