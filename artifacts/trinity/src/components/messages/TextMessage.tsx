import React from 'react';

export function TextMessage({ content }: { content: string }) {
  return <div className="whitespace-pre-wrap font-mono text-sm leading-relaxed">{content}</div>;
}