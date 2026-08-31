import React, { useState, useRef } from 'react';
import { useSendChat } from '@workspace/api-client-react';
import { useQueryClient } from '@tanstack/react-query';

export function MessageInput({ conversationId, onConversationCreated }: { conversationId: number | null, onConversationCreated: (id: number) => void }) {
  const [content, setContent] = useState('');
  const sendChat = useSendChat();
  const queryClient = useQueryClient();
  const inputRef = useRef<HTMLTextAreaElement>(null);

  const handleSubmit = (e?: React.FormEvent) => {
    e?.preventDefault();
    if (!content.trim() || sendChat.isPending) return;

    sendChat.mutate({ 
      data: { 
        content, 
        conversation_id: conversationId 
      } 
    }, {
      onSuccess: (res) => {
        setContent('');
        queryClient.invalidateQueries({ queryKey: ['/api/conversations'] });
        if (res.conversation_id) {
          if (!conversationId) {
            onConversationCreated(res.conversation_id);
          }
          // Defer invalidation slightly to ensure the new ChatThread has mounted and registered its query
          setTimeout(() => {
            queryClient.invalidateQueries({ queryKey: [`/api/conversations/${res.conversation_id}/messages`] });
          }, 0);
        }
      }
    });
  };

  const handleKeyDown = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSubmit();
    }
  };

  return (
    <form onSubmit={handleSubmit} className="p-4 md:p-6 bg-background border-t border-border flex gap-4 items-end relative">
      <div className="absolute top-0 left-0 w-full h-[1px] bg-gradient-to-r from-transparent via-primary/50 to-transparent"></div>
      
      <div className="flex-1 relative flex items-center bg-input border border-border rounded focus-within:border-primary/50 focus-within:ring-1 focus-within:ring-primary/20 transition-all">
        <div className="pl-4 text-primary font-mono text-sm opacity-50">&gt;</div>
        <textarea
          ref={inputRef}
          value={content}
          onChange={(e) => setContent(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder="ENTER COMMAND OR QUERY..."
          className="flex-1 min-h-[44px] max-h-48 py-3 px-3 bg-transparent border-none focus:outline-none focus:ring-0 resize-none font-mono text-sm scrollbar-hide text-foreground placeholder:text-muted-foreground/50"
          rows={1}
          disabled={sendChat.isPending}
          autoFocus
        />
      </div>
      
      <button 
        type="submit" 
        disabled={!content.trim() || sendChat.isPending} 
        className="h-[44px] px-6 bg-primary text-primary-foreground font-mono text-sm font-bold rounded hover:bg-primary/90 disabled:opacity-50 disabled:cursor-not-allowed transition-colors flex items-center gap-2 uppercase tracking-wider"
      >
        {sendChat.isPending ? (
          <>
            <span className="w-4 h-4 border-2 border-primary-foreground/30 border-t-primary-foreground rounded-full animate-spin"></span>
            PROCESS
          </>
        ) : (
          <>
            SEND <span className="text-primary-foreground/50 opacity-50 text-lg leading-none pt-0.5">↵</span>
          </>
        )}
      </button>
    </form>
  );
}