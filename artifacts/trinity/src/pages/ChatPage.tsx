import React, { useState, useEffect } from 'react';
import { Sidebar } from '@/components/Sidebar';
import { ChatThread } from '@/components/ChatThread';
import { MessageInput } from '@/components/MessageInput';
import { useListConversations } from '@workspace/api-client-react';

export function ChatPage() {
  const [activeConversationId, setActiveConversationId] = useState<number | null>(null);
  const [extraMessages, setExtraMessages] = useState<any[]>([]);

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
        <ChatThread conversationId={activeConversationId} extraMessages={extraMessages} />
        <MessageInput 
          conversationId={activeConversationId} 
          onConversationCreated={setActiveConversationId}
          onVisionResult={(message) => setExtraMessages((current) => [...current, message])}
        />
      </main>
    </div>
  );
}