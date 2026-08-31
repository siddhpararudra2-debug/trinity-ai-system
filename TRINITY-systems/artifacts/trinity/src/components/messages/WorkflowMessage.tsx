export function WorkflowMessage({ data }: { data: any }) {
  const workflow = data?.workflow;
  if (!workflow?.steps?.length) return null;
  return (
    <div className="flex flex-col gap-3 font-mono text-xs">
      <div className="text-primary font-bold uppercase tracking-widest">WORKFLOW PLAN · {workflow.mode || 'deterministic'}</div>
      <div className="text-muted-foreground">{workflow.objective}</div>
      <div className="flex flex-col gap-2">
        {workflow.steps.map((step: any, index: number) => (
          <div key={step.id} className="rounded border border-border bg-background/40 p-3">
            <div className="text-primary">{index + 1}. {step.engine}</div>
            <div className="mt-1">{step.objective}</div>
            {step.depends_on?.length > 0 && <div className="mt-1 text-[10px] text-muted-foreground">Depends on: {step.depends_on.join(', ')}</div>}
          </div>
        ))}
      </div>
    </div>
  );
}
