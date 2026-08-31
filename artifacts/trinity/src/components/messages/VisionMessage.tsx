import { TextMessage } from './TextMessage';

export function VisionMessage({ data }: { data: any }) {
  if (data?.status === 'error') {
    return <TextMessage content={`Vision OCR error: ${data.message || 'OCR processing failed.'}`} />;
  }
  return (
    <div className="flex flex-col gap-3 font-mono text-xs">
      <div className="text-primary font-bold uppercase tracking-widest">VISION OCR</div>
      {data?.recognized_text ? (
        <div className="rounded border border-border bg-background/40 p-3 whitespace-pre-wrap">{data.recognized_text}</div>
      ) : (
        <div className="text-muted-foreground">No text recognized.</div>
      )}
      {data?.latex && (
        <div className="rounded border border-primary/20 bg-primary/5 p-3">
          <div className="text-[10px] text-primary/70 mb-1 uppercase">LaTeX</div>
          <code className="whitespace-pre-wrap">{data.latex}</code>
        </div>
      )}
      {data?.ocr_backend && <div className="text-[10px] text-muted-foreground">Backend: {data.ocr_backend}</div>}
    </div>
  );
}
