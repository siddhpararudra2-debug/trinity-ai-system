import React, { useRef, useState } from 'react';
import { useSendChat } from '@workspace/api-client-react';
import { useQueryClient } from '@tanstack/react-query';
import { ImagePlus } from 'lucide-react';
import { getAuthHeaders } from '@/lib/auth';

type MessageInputProps = {
  conversationId: number | null;
  selectedEngine: string | null;
  onSelectedEngineChange: (engine: string | null) => void;
  onConversationCreated: (id: number) => void;
  onVisionResult?: (message: any) => void;
};

export function MessageInput({
  conversationId,
  selectedEngine,
  onSelectedEngineChange,
  onConversationCreated,
  onVisionResult,
}: MessageInputProps) {
  const [content, setContent] = useState('');
  const [visionPending, setVisionPending] = useState(false);
  const sendChat = useSendChat();
  const queryClient = useQueryClient();
  const inputRef = useRef<HTMLTextAreaElement>(null);
  const fileRef = useRef<HTMLInputElement>(null);

  const handleSubmit = (event?: React.FormEvent) => {
    event?.preventDefault();
    if (!content.trim() || sendChat.isPending || visionPending) return;

    sendChat.mutate(
      { data: { content, conversation_id: conversationId, engine: selectedEngine } },
      {
        onSuccess: (response) => {
          setContent('');
          queryClient.invalidateQueries({ queryKey: ['/api/conversations'] });
          queryClient.invalidateQueries({
            queryKey: [`/api/conversations/${response.conversation_id}/messages`],
          });
          if (!conversationId) onConversationCreated(response.conversation_id);
        },
      },
    );
  };

  const handleVisionUpload = async (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    event.target.value = '';
    if (!file || visionPending) return;

    setVisionPending(true);
    try {
      if (!conversationId) {
        throw new Error('Create or select a conversation before uploading an image.');
      }
      const form = new FormData();
      form.append('file', file);
      form.append('description', content);
      form.append('conversation_id', String(conversationId));
      const response = await fetch('/api/vision/ocr', {
        method: 'POST',
        headers: getAuthHeaders(),
        body: form,
      });
      const data = await response.json();
      if (!response.ok) throw new Error(data.detail || 'Vision upload failed');
      queryClient.invalidateQueries({ queryKey: ['/api/conversations'] });
      queryClient.invalidateQueries({
        queryKey: [`/api/conversations/${conversationId}/messages`],
      });
      setContent('');
    } catch (error: any) {
      onVisionResult?.({
        id: `vision-error-${Date.now()}`,
        role: 'assistant',
        engine: 'vision',
        content: error?.message || 'Vision upload failed',
        conversation_id: conversationId,
        data: { status: 'error', message: error?.message || 'Vision upload failed' },
        created_at: new Date().toISOString(),
      });
    } finally {
      setVisionPending(false);
    }
  };

  const handleKeyDown = (event: React.KeyboardEvent<HTMLTextAreaElement>) => {
    if (event.key === 'Enter' && !event.shiftKey) {
      event.preventDefault();
      handleSubmit();
    }
  };

  const busy = sendChat.isPending || visionPending;
  return (
    <form
      onSubmit={handleSubmit}
      className="relative flex items-end gap-4 border-t border-border bg-background p-4 md:p-6"
    >
      <div className="absolute left-0 top-0 h-px w-full bg-gradient-to-r from-transparent via-primary/50 to-transparent" />
      <input
        ref={fileRef}
        type="file"
        accept="image/png,image/jpeg,image/jpg,image/bmp,image/tiff,image/webp"
        onChange={handleVisionUpload}
        className="hidden"
      />
      <button
        type="button"
        title="Upload image for OCR"
        onClick={() => fileRef.current?.click()}
        disabled={busy}
        className="flex h-11 w-11 items-center justify-center rounded border border-border text-primary hover:bg-white/5 disabled:opacity-50"
      >
        {visionPending ? (
          <span className="h-4 w-4 animate-spin rounded-full border-2 border-primary/30 border-t-primary" />
        ) : (
          <ImagePlus className="h-4 w-4" />
        )}
      </button>
      <select
        aria-label="Engine override"
        value={selectedEngine ?? ''}
        onChange={(event) => onSelectedEngineChange(event.target.value || null)}
        disabled={busy}
        className="h-11 max-w-[150px] rounded border border-border bg-input px-2 text-[10px] uppercase tracking-widest text-primary focus:outline-none focus:ring-1 focus:ring-primary/30"
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
      <div className="relative flex flex-1 items-center rounded border border-border bg-input transition-all focus-within:border-primary/50 focus-within:ring-1 focus-within:ring-primary/20">
        <div className="pl-4 font-mono text-sm text-primary opacity-50">&gt;</div>
        <textarea
          ref={inputRef}
          value={content}
          onChange={(event) => setContent(event.target.value)}
          onKeyDown={handleKeyDown}
          placeholder="ENTER COMMAND OR QUERY..."
          className="min-h-11 max-h-48 flex-1 resize-none border-none bg-transparent px-3 py-3 font-mono text-sm text-foreground outline-none placeholder:text-muted-foreground/50 focus:ring-0"
          rows={1}
          disabled={busy}
          autoFocus
        />
      </div>
      <button
        type="submit"
        disabled={!content.trim() || busy}
        className="flex h-11 items-center gap-2 rounded bg-primary px-6 font-mono text-sm font-bold uppercase tracking-wider text-primary-foreground transition-colors hover:bg-primary/90 disabled:cursor-not-allowed disabled:opacity-50"
      >
        {sendChat.isPending ? (
          <>
            <span className="h-4 w-4 animate-spin rounded-full border-2 border-primary-foreground/30 border-t-primary-foreground" />
            PROCESS
          </>
        ) : (
          <>SEND <span className="text-lg leading-none text-primary-foreground/50">↵</span></>
        )}
      </button>
    </form>
  );
}
