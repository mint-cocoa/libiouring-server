const PREVIEW_BASE = import.meta.env.VITE_PREVIEW_BASE ?? "/preview";

interface PreviewPaneProps {
  slug: string;
  content: string;
}

export function PreviewPane({ slug }: PreviewPaneProps) {
  if (!slug) {
    return (
      <div className="h-full flex items-center justify-center text-gray-400 text-sm">
        문서를 선택하세요
      </div>
    );
  }

  return (
    <iframe
      src={`${PREVIEW_BASE}/${slug}.html`}
      className="w-full h-full border-0"
      title="Quarto Preview"
    />
  );
}
