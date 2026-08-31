import React from 'react';
import { MathMessage } from './messages/MathMessage';
import { QuantumMessage } from './messages/QuantumMessage';
import { MakerMessage } from './messages/MakerMessage';
import { LiteratureMessage } from './messages/LiteratureMessage';
import { TextMessage } from './messages/TextMessage';

export function MessageBubble({ message }: { message: any }) {
  const isUser = message.role === 'user';
  
  return (
    <div className={`flex w-full ${isUser ? 'justify-end' : 'justify-start'} mb-8 group`}>
      <div className={`flex flex-col max-w-[85%] ${isUser ? 'items-end' : 'items-start'}`}>
        <div className={`flex items-center gap-2 mb-2 px-1 text-[10px] uppercase tracking-wider ${isUser ? 'text-accent' : 'text-primary'}`}>
          {isUser ? 'USER_INPUT' : message.engine ? `SYS_${message.engine.toUpperCase()}` : 'SYS_CORE'}
          <span className="opacity-30">{new Date(message.created_at).toLocaleTimeString()}</span>
        </div>
        <div className={`p-5 rounded-md w-full ${
          isUser 
            ? 'bg-accent/10 border border-accent/20 text-accent-foreground' 
            : 'bg-card border border-border shadow-sm'
        }`}>
          {isUser ? (
            <TextMessage content={message.content} />
          ) : (
            <MessageContent message={message} />
          )}
        </div>
      </div>
    </div>
  );
}

function MessageContent({ message }: { message: any }) {
  if (!message.data) return <TextMessage content={message.content} />;
  
  if (message.data.latex || message.data.expression) return <MathMessage data={message.data} />;
  if (message.data.circuit_diagram || message.data.counts) return <QuantumMessage data={message.data} />;
  if (message.data.script || message.data.sch_content) return <MakerMessage data={message.data} />;
  if (message.data.papers) return <LiteratureMessage data={message.data} />;
  
  return <TextMessage content={message.content} />;
}