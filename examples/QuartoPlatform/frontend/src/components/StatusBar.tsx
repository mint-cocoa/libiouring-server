type SaveStatus = "idle" | "saving" | "saved" | "error";

interface StatusBarProps {
  status: SaveStatus;
  lastSaved: Date | null;
  onPublish: () => void;
  onUnpublish: () => void;
  isPublished: boolean;
}

function formatTimeAgo(date: Date): string {
  const seconds = Math.floor((Date.now() - date.getTime()) / 1000);
  if (seconds < 5) return "방금 전";
  if (seconds < 60) return `${seconds}초 전`;
  const minutes = Math.floor(seconds / 60);
  return `${minutes}분 전`;
}

export function StatusBar({
  status,
  lastSaved,
  onPublish,
  onUnpublish,
  isPublished,
}: StatusBarProps) {
  return (
    <div className="flex items-center justify-between px-4 py-2 border-t border-gray-200 bg-gray-50 text-sm">
      <div className="text-gray-500">
        {status === "saving" && "저장 중..."}
        {status === "saved" && lastSaved && `✓ 저장됨 (${formatTimeAgo(lastSaved)})`}
        {status === "error" && <span className="text-red-500">⚠ 저장 실패</span>}
        {status === "idle" && ""}
      </div>
      <div className="flex gap-2">
        {isPublished ? (
          <button
            onClick={onUnpublish}
            className="px-3 py-1 text-sm text-red-600 border border-red-300 rounded hover:bg-red-50"
          >
            발행 취소
          </button>
        ) : (
          <button
            onClick={onPublish}
            className="px-3 py-1 text-sm text-white bg-green-600 rounded hover:bg-green-700"
          >
            발행
          </button>
        )}
      </div>
    </div>
  );
}
