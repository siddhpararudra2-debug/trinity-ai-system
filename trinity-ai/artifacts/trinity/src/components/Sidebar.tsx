import React from 'react';
import { useListConversations, useCreateConversation, useDeleteConversation, useHealthCheck } from '@workspace/api-client-react';
import { Plus, MessageSquare, Trash2 } from 'lucide-react';
import { useQueryClient } from '@tanstack/react-query';
import { EngineRegistry } from './EngineRegistry';

export function Sidebar({ activeId, onSelect }: { activeId: number | null, onSelect: (id: number | null) => void }) {
  const { data: conversations } = useListConversations();
  const createConv = useCreateConversation();
  const deleteConv = useDeleteConversation();
  const queryClient = useQueryClient();
  const { data: health } = useHealthCheck();

  const handleCreate = () => {
    createConv.mutate({ data: { title: 'New Analysis' } }, {
      onSuccess: (res) => {
        queryClient.invalidateQueries({ queryKey: ['/api/conversations'] });
        onSelect(res.id);
      }
    });
  };

  const handleDelete = (e: React.MouseEvent, id: number) => {
    e.stopPropagation();
    if (window.confirm('Are you sure you want to terminate this process?')) {
      deleteConv.mutate({ id }, {
        onSuccess: () => {
          queryClient.invalidateQueries({ queryKey: ['/api/conversations'] });
          if (activeId === id) {
            onSelect(null);
          }
        }
      });
    }
  };

  return (
    <div className="w-72 flex-shrink-0 flex flex-col bg-sidebar border-r border-border font-mono relative z-10 shadow-xl">
      <div className="p-6 border-b border-border flex flex-col gap-4 bg-black/20">
        <div className="flex items-center justify-between">
          <h1 className="text-primary font-bold tracking-widest text-xl drop-shadow-[0_0_8px_hsl(var(--primary)/0.5)]">TRINITY.OS</h1>
          <div className="flex items-center gap-2">
            {health?.status === 'ok' && health?.db === 'healthy' ? (
              <div className="w-2 h-2 rounded-full bg-primary shadow-[0_0_8px_hsl(var(--primary))] animate-pulse" title="System Online" />
            ) : (
              <div className="w-2 h-2 rounded-full bg-destructive shadow-[0_0_8px_hsl(var(--destructive))]" title="System Offline" />
            )}
          </div>
        </div>
        
        <button 
          onClick={handleCreate} 
          disabled={createConv.isPending} 
          className="w-full flex items-center justify-center gap-2 bg-primary/10 text-primary hover:bg-primary/20 hover:text-primary border border-primary/30 rounded py-2.5 text-xs font-bold tracking-widest transition-all disabled:opacity-50"
        >
          <Plus className="w-3.5 h-3.5" />
          NEW PROCESS
        </button>
      </div>

      <div className="flex-1 overflow-y-auto p-3 space-y-1 scrollbar-hide">
        <div className="text-[10px] font-mono font-bold text-muted-foreground uppercase tracking-widest mb-3 px-2 mt-2">ACTIVE THREADS</div>
        {conversations?.map(conv => (
          <div 
            key={conv.id}
            onClick={() => onSelect(conv.id)}
            className={`flex items-center justify-between p-3 rounded cursor-pointer transition-all group ${
              activeId === conv.id 
                ? 'bg-accent/10 text-accent border border-accent/20' 
                : 'text-muted-foreground border border-transparent hover:bg-white/5 hover:text-foreground hover:border-white/10'
            }`}
          >
            <div className="flex items-center gap-3 overflow-hidden">
              <MessageSquare className={`w-4 h-4 flex-shrink-0 ${activeId === conv.id ? 'text-accent' : 'opacity-50'}`} />
              <div className="flex flex-col overflow-hidden">
                <span className="truncate text-xs font-bold tracking-wide">{conv.title || `Thread ${conv.id}`}</span>
                <span className="text-[9px] opacity-50 truncate">ID: {conv.id.toString().padStart(4, '0')}</span>
              </div>
            </div>
            <button 
              onClick={(e) => handleDelete(e, conv.id)}
              className="opacity-0 group-hover:opacity-100 hover:text-destructive hover:bg-destructive/20 p-1.5 rounded transition-colors"
              title="Terminate Process"
            >
              <Trash2 className="w-3.5 h-3.5" />
            </button>
          </div>
        ))}
        {conversations?.length === 0 && (
          <div className="text-xs text-muted-foreground opacity-50 text-center py-8">
            NO ACTIVE THREADS
          </div>
        )}
      </div>

      <EngineRegistry />
    </div>
  );
}