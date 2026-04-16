import { useState, useEffect, useCallback } from "react";
import { Header } from "../components/Header";
import { Sidebar } from "../components/Sidebar";
import { MarkdownEditor } from "../components/MarkdownEditor";
import { PreviewPane } from "../components/PreviewPane";
import { StatusBar } from "../components/StatusBar";
import { Toast } from "../components/Toast";
import { useDocuments } from "../hooks/useDocuments";
import { useAutoSave } from "../hooks/useAutoSave";
import { usePublish } from "../hooks/usePublish";

interface EditorPageProps {
  userId: string;
  onLogout: () => void;
}

export function EditorPage({ userId, onLogout }: EditorPageProps) {
  const { documents, refresh, getDocument, saveDocument, deleteDocument } =
    useDocuments();
  const { published, refresh: refreshPublished, publish, unpublish } =
    usePublish();

  const [activeSlug, setActiveSlug] = useState<string | null>(null);
  const [content, setContent] = useState("");
  const [toast, setToast] = useState<string | null>(null);

  const saveFn = useCallback(
    async (text: string) => {
      if (activeSlug) {
        await saveDocument(activeSlug, text);
      }
    },
    [activeSlug, saveDocument],
  );

  const { trigger, status, lastSaved } = useAutoSave(saveFn);

  const handleSelect = async (slug: string) => {
    setActiveSlug(slug);
    const doc = await getDocument(slug);
    setContent(doc);
  };

  const handleCreate = async () => {
    const slug = prompt("글 slug를 입력하세요 (예: my-first-post)");
    if (!slug) return;
    const template = `---\ntitle: "${slug}"\ndate: ${new Date().toISOString().split("T")[0]}\n---\n\n## ${slug}\n\n내용을 작성하세요.\n`;
    await saveDocument(slug, template);
    await refresh();
    await handleSelect(slug);
  };

  const handleDelete = async (slug: string) => {
    if (!confirm(`"${slug}"을 삭제하시겠습니까?`)) return;
    await deleteDocument(slug);
    if (activeSlug === slug) {
      setActiveSlug(null);
      setContent("");
    }
  };

  const handleChange = (value: string) => {
    setContent(value);
    trigger(value);
  };

  const handlePublish = async () => {
    if (!activeSlug) return;
    const urlPath = await publish(activeSlug);
    const fullUrl = `https://blog.mintcocoa.cc${urlPath}`;
    await navigator.clipboard.writeText(fullUrl);
    setToast(`발행 완료! URL이 클립보드에 복사되었습니다: ${fullUrl}`);
  };

  const handleUnpublish = async () => {
    if (!activeSlug) return;
    await unpublish(activeSlug);
    setToast("발행이 취소되었습니다.");
  };

  useEffect(() => {
    refreshPublished();
  }, [refreshPublished]);

  const isPublished = published.some((p) => p.slug === activeSlug);

  return (
    <div className="h-screen flex flex-col">
      <Header userId={userId} onLogout={onLogout} />
      <div className="flex-1 flex overflow-hidden">
        <Sidebar
          documents={documents}
          activeSlug={activeSlug}
          onSelect={handleSelect}
          onCreate={handleCreate}
          onDelete={handleDelete}
        />
        {activeSlug ? (
          <div className="flex-1 flex flex-col">
            <div className="flex-1 flex overflow-hidden">
              <div className="flex-1 overflow-hidden">
                <MarkdownEditor value={content} onChange={handleChange} />
              </div>
              <div className="flex-1 border-l border-gray-200 h-full">
                <PreviewPane slug={activeSlug} content={content} />
              </div>
            </div>
            <StatusBar
              status={status}
              lastSaved={lastSaved}
              onPublish={handlePublish}
              onUnpublish={handleUnpublish}
              isPublished={isPublished}
            />
          </div>
        ) : (
          <div className="flex-1 flex items-center justify-center text-gray-400">
            왼쪽에서 글을 선택하거나 새 글을 만드세요
          </div>
        )}
      </div>
      <Toast message={toast} onClose={() => setToast(null)} />
    </div>
  );
}
