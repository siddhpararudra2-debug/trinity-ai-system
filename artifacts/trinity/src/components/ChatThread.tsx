import React, { useEffect, useRef } from 'react';
import { useGetMessages } from '@workspace/api-client-react';
import { MessageBubble } from './MessageBubble';

export function ChatThread({ conversationId, extraMessages = [] }: { conversationId: number | null; extraMessages?: any[] }) {
  const { data: messages, isLoading } = useGetMessages(conversationId as number, { query: { queryKey: ['/api/conversations', conversationId, 'messages'], enabled: !!conversationId } });
  const scrollRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [messages, extraMessages]);

  if (!conversationId) {
    return (
      <div className="flex-1 flex flex-col items-center justify-center text-muted-foreground font-mono text-sm">
        <div className="w-16 h-16 border border-dashed border-primary/30 rounded-full flex items-center justify-center mb-6 relative">
          <div className="absolute inset-2 border border-primary/20 rounded-full animate-ping"></div>
          <div className="w-2 h-2 bg-primary rounded-full animate-pulse"></div>
        </div>
        <div className="tracking-widest text-primary mb-2 text-lg font-bold">TRINITY.OS_v1.0</div>
        <div className="opacity-50 text-xs text-center max-w-sm mt-4 leading-relaxed">
          AWAITING INITIALIZATION SEQUENCE.<br />SELECT OR CREATE A PROCESS TO BEGIN.
        </div>
      </div>
    );
  }

  return (
    <div className="flex-1 overflow-y-auto p-4 md:p-8 scrollbar-hide relative" ref={scrollRef}>
      {isLoading ? (
        <div className="flex justify-center mt-20">
          <div className="flex items-center gap-3 text-primary font-mono text-sm">
            <span className="w-4 h-4 border-2 border-primary/30 border-t-primary rounded-full animate-spin"></span>
            RETRIEVING LOGS...
          </div>
        </div>
      ) : (
        <div className="max-w-4xl mx-auto flex flex-col min-h-full justify-end">
          {[...(messages || []), ...extraMessages].map((msg: any) => (
            <MessageBubble key={msg.id} message={msg} />
          ))}
          {messages?.length === 0 && extraMessages.length === 0 && (
            <div className="text-center text-muted-foreground font-mono text-sm mt-auto mb-auto opacity-50 py-10">
              CONNECTION ESTABLISHED. AWAITING INPUT.
            </div>
          )}
        </div>
      )}
    </div>
  );
}