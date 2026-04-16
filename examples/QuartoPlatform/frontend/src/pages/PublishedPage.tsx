import { useEffect, useState } from "react";
import { Header } from "../components/Header";
import { usePublish } from "../hooks/usePublish";
import { Toast } from "../components/Toast";

interface PublishedPageProps {
  userId: string;
  onLogout: () => void;
}

export function PublishedPage({ userId, onLogout }: PublishedPageProps) {
  const { published, loading, refresh, unpublish } = usePublish();
  const [toast, setToast] = useState<string | null>(null);

  useEffect(() => {
    refresh();
  }, [refresh]);

  const copyUrl = (urlPath: string) => {
    const fullUrl = `https://blog.mintcocoa.cc${urlPath}`;
    navigator.clipboard.writeText(fullUrl);
    setToast("URL이 클립보드에 복사되었습니다!");
  };

  const handleUnpublish = async (slug: string) => {
    if (!confirm(`"${slug}" 발행을 취소하시겠습니까?`)) return;
    await unpublish(slug);
  };

  return (
    <div className="h-screen flex flex-col">
      <Header userId={userId} onLogout={onLogout} />
      <div className="flex-1 overflow-auto p-6 bg-gray-50">
        <h2 className="text-xl font-bold text-gray-900 mb-4">발행된 글</h2>
        {loading ? (
          <p className="text-gray-500">로딩 중...</p>
        ) : published.length === 0 ? (
          <p className="text-gray-500">발행된 글이 없습니다.</p>
        ) : (
          <div className="bg-white rounded-lg border border-gray-200">
            {published.map((item) => (
              <div
                key={item.slug}
                className="flex items-center justify-between px-4 py-3 border-b border-gray-100 last:border-b-0"
              >
                <div>
                  <p className="font-medium text-gray-900">{item.slug}</p>
                  <p className="text-sm text-blue-600">
                    blog.mintcocoa.cc{item.url_path}
                  </p>
                </div>
                <div className="flex gap-2">
                  <button
                    onClick={() => copyUrl(item.url_path)}
                    className="px-3 py-1 text-sm text-gray-600 border border-gray-300 rounded hover:bg-gray-50"
                  >
                    URL 복사
                  </button>
                  <button
                    onClick={() => handleUnpublish(item.slug)}
                    className="px-3 py-1 text-sm text-red-600 border border-red-300 rounded hover:bg-red-50"
                  >
                    발행 취소
                  </button>
                </div>
              </div>
            ))}
          </div>
        )}
      </div>
      <Toast message={toast} onClose={() => setToast(null)} />
    </div>
  );
}
