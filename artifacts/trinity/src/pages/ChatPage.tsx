import React, { useEffect, useState } from 'react';
import { AuthPanel } from '@/components/AuthPanel';
import { Sidebar } from '@/components/Sidebar';
import { ChatThread } from '@/components/ChatThread';
import { MessageInput } from '@/components/MessageInput';
import { OperationsPanel } from '@/components/OperationsPanel';
import { useListConversations } from '@workspace/api-client-react';

export function ChatPage() {
  const [activeConversationId, setActiveConversationId] = useState<number | null>(null);
  const [extraMessages, setExtraMessages] = useState<any[]>([]);
  const [selectedEngine, setSelectedEngine] = useState<string | null>(null);

  const { data: conversations } = useListConversations();
  useEffect(() => {
    if (!activeConversationId && conversations && conversations.length > 0) {
      setActiveConversationId(conversations[0].id);
    }
  }, [conversations, activeConversationId]);

  return (
    <div className="flex h-screen w-full bg-background overflow-hidden selection:bg-primary/30 selection:text-primary">
      <Sidebar 
        activeId={activeConversationId} 
        onSelect={setActiveConversationId} 
      />
      <main className="flex-1 flex flex-col h-full overflow-hidden relative">
        <div className="absolute right-4 top-4 z-20 flex flex-col items-end gap-2">
          <AuthPanel onAuthChanged={() => window.location.reload()} />
          <OperationsPanel />
        </div>
        <ChatThread conversationId={activeConversationId} extraMessages={extraMessages} />
        <MessageInput 
          conversationId={activeConversationId} 
          selectedEngine={selectedEngine}
          onSelectedEngineChange={setSelectedEngine}
          onConversationCreated={setActiveConversationId}
          onVisionResult={(message) => setExtraMessages((current) => [...current, message])}
        />
      </main>
    </div>
  );
}