# Trinity web application

This is the production React/Vite client for Trinity AI. It is the `/` SPA that talks to the FastAPI service through `/api`.

## Commands

```bash
pnpm --filter @workspace/trinity run dev
pnpm --filter @workspace/trinity run typecheck
pnpm --filter @workspace/trinity run build
pnpm --filter @workspace/trinity run serve
```

The dev proxy in [`vite.config.ts`](vite.config.ts) targets `http://localhost:8000`.

## Source map

- `src/pages/ChatPage.tsx` — page composition and active conversation state.
- `src/components/MessageInput.tsx` — chat submit, engine override, and image upload.
- `src/components/ChatThread.tsx` — persisted message history.
- `src/components/Sidebar.tsx` — conversations and engine registry.
- `src/components/AuthPanel.tsx` — registration/login and local bearer-token storage.
- `src/components/OperationsPanel.tsx` — health and durable-job polling/cancellation.
- `src/components/MessageBubble.tsx` and `src/components/messages/` — response dispatch and engine renderers.
- `src/components/ui/` — reusable presentation components, not backend behavior.
- `src/main.tsx` — React bootstrap and generated-client auth-token hook.

The current UI has no WebSocket collaboration client, no PDF/HEIC upload path, and no general AI chat provider. See [`../../docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md).
