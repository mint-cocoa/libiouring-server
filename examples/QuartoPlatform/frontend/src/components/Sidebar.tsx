interface DocMeta {
  slug: string;
  title: string;
  date: string;
}

interface SidebarProps {
  documents: DocMeta[];
  activeSlug: string | null;
  onSelect: (slug: string) => void;
  onCreate: () => void;
  onDelete: (slug: string) => void;
}

export function Sidebar({
  documents,
  activeSlug,
  onSelect,
  onCreate,
  onDelete,
}: SidebarProps) {
  return (
    <aside className="w-56 border-r border-gray-200 bg-gray-50 flex flex-col">
      <div className="p-3 border-b border-gray-200">
        <button
          onClick={onCreate}
          className="w-full px-3 py-1.5 text-sm bg-blue-600 text-white rounded hover:bg-blue-700"
        >
          + 새 글
        </button>
      </div>
      <nav className="flex-1 overflow-y-auto">
        {documents.map((doc) => (
          <div
            key={doc.slug}
            className={`group flex items-center justify-between px-3 py-2 cursor-pointer text-sm ${
              activeSlug === doc.slug
                ? "bg-blue-50 text-blue-700"
                : "text-gray-700 hover:bg-gray-100"
            }`}
            onClick={() => onSelect(doc.slug)}
          >
            <span className="truncate">{doc.title || doc.slug}</span>
            <button
              onClick={(e) => {
                e.stopPropagation();
                onDelete(doc.slug);
              }}
              className="hidden group-hover:block text-gray-400 hover:text-red-500 text-xs"
            >
              ✕
            </button>
          </div>
        ))}
      </nav>
    </aside>
  );
}
