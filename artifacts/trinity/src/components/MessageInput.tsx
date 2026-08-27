import React, { useRef, useState } from 'react';
import { useSendChat } from '@workspace/api-client-react';
import { useQueryClient } from '@tanstack/react-query';
import { ImagePlus } from 'lucide-react';

type MessageInputProps = {
  conversationId: number | null;
  selectedEngine: string | null;
  onSelectedEngineChange: (engine: string | null) => void;
  onConversationCreated: (id: number) => void;
  onVisionResult?: (message: any) => void;
};

export function MessageInput({ conversationId, selectedEngine, onSelectedEngineChange, onConversationCreated, onVisionResult }: MessageInputProps) {
  const [content, setContent] = useState('');
  const [visionPending, setVisionPending] = useState(false);
  const sendChat = useSendChat();
  const queryClient = useQueryClient();
  const inputRef = useRef<HTMLTextAreaElement>(null);
  const fileRef = useRef<HTMLInputElement>(null);

  const handleSubmit = (e?: React.FormEvent) => {
    e?.preventDefault();
    if (!content.trim() || sendChat.isPending || visionPending) return;

    sendChat.mutate({ data: { content, conversation_id: conversationId, engine: selectedEngine } }, {
      onSuccess: (res) => {
        setContent('');
        queryClient.invalidateQueries({ queryKey: ['/api/conversations'] });
        if (res.conversation_id) {
          queryClient.invalidateQueries({ queryKey: [`/api/conversations/${res.conversation_id}/messages`] });
          if (!conversationId) onConversationCreated(res.conversation_id);
        }
      },
    });
  };

  const handleVisionUpload = async (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    event.target.value = '';
    if (!file || visionPending) return;
    setVisionPending(true);
    try {
      const form = new FormData();
      form.append('file', file);
      form.append('description', content);
      const response = await fetch('/api/vision/ocr', { method: 'POST', body: form });
      const data = await response.json();
      if (!response.ok) throw new Error(data.detail || 'Vision upload failed');
      onVisionResult?.({
        id: `vision-${Date.now()}`,
        role: 'assistant',
        engine: 'vision',
        content: data.description || 'Vision OCR result',
        data,
        created_at: new Date().toISOString(),
      });
      setContent('');
    } catch (error: any) {
      onVisionResult?.({
        id: `vision-error-${Date.now()}`,
        role: 'assistant',
        engine: 'vision',
        content: error?.message || 'Vision upload failed',
        data: { status: 'error', message: error?.message || 'Vision upload failed' },
        created_at: new Date().toISOString(),
      });
    } finally {
      setVisionPending(false);
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSubmit();
    }
  };

  const busy = sendChat.isPending || visionPending;
  return (
    <form onSubmit={handleSubmit} className="p-4 md:p-6 bg-background border-t border-border flex gap-4 items-end relative">
      <div className="absolute top-0 left-0 w-full h-[1px] bg-gradient-to-r from-transparent via-primary/50 to-transparent" />
      <input ref={fileRef} type="file" accept="image/png,image/jpeg,image/jpg,image/bmp,image/tiff,image/webp" onChange={handleVisionUpload} className="hidden" />
      <button type="button" title="Upload image for OCR" onClick={() => fileRef.current?.click()} disabled={busy} className="h-[44px] w-[44px] border border-border rounded text-primary hover:bg-white/5 disabled:opacity-50 flex items-center justify-center">

        {visionPending ? <span className="w-4 h-4 border-2 border-primary/30 border-t-primary rounded-full animate-spin" /> : <ImagePlus className="w-4 h-4" />}
      </button>
      <select
        aria-label="Engine override"
        value={selectedEngine ?? ''}
        onChange={(event) => onSelectedEngineChange(event.target.value || null)}
        disabled={busy}
        className="h-[44px] max-w-[150px] bg-input border border-border rounded px-2 text-[10px] uppercase tracking-widest text-primary focus:outline-none focus:ring-1 focus:ring-primary/30"
      >
        <option value="">AUTO ROUTE</option>
        <option value="math">MATH</option>
        <option value="quantum">QUANTUM</option>
        <option value="maker_cad">CAD</option>
        <option value="maker_pcb">PCB</option>
        <option value="literature">LITERATURE</option>
        <option value="firmware">FIRMWARE</option>
        <option value="vision">VISION</option>
        <option value="collab">COLLAB</option>
      </select>
      <div className="flex-1 relative flex items-center bg-input border border-border rounded focus-within:border-primary/50 focus-within:ring-1 focus-within:ring-primary/20 transition-all">
        <div className="pl-4 text-primary font-mono text-sm opacity-50">&gt;</div>
        <textarea ref={inputRef} value={content} onChange={(e) => setContent(e.target.value)} onKeyDown={handleKeyDown} placeholder="ENTER COMMAND OR QUERY..." className="flex-1 min-h-[44px] max-h-48 py-3 px-3 bg-transparent border-none focus:outline-none focus:ring-0 resize-none font-mono text-sm scrollbar-hide text-foreground placeholder:text-muted-foreground/50" rows={1} disabled={busy} autoFocus />
      </div>
      <button type="submit" disabled={!content.trim() || busy} className="h-[44px] px-6 bg-primary text-primary-foreground font-mono text-sm font-bold rounded hover:bg-primary/90 disabled:opacity-50 disabled:cursor-not-allowed transition-colors flex items-center gap-2 uppercase tracking-wider">
        {sendChat.isPending ? <><span className="w-4 h-4 border-2 border-primary-foreground/30 border-t-primary-foreground rounded-full animate-spin" />PROCESS</> : <>SEND <span className="text-primary-foreground/50 opacity-50 text-lg leading-none pt-0.5">↵</span></>}
      </button>
    </form>
  );
}
