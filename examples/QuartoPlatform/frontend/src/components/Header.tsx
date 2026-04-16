import { Link } from "react-router-dom";

interface HeaderProps {
  userId: string | null;
  onLogout: () => void;
}

export function Header({ userId, onLogout }: HeaderProps) {
  return (
    <header className="flex items-center justify-between px-4 py-2 border-b border-gray-200 bg-white">
      <div className="flex items-center gap-4">
        <Link to="/editor" className="text-lg font-bold text-gray-900">
          Quarto
        </Link>
        <nav className="flex gap-2">
          <Link
            to="/editor"
            className="px-3 py-1 text-sm text-gray-600 hover:text-gray-900 rounded hover:bg-gray-100"
          >
            내 글
          </Link>
          <Link
            to="/published"
            className="px-3 py-1 text-sm text-gray-600 hover:text-gray-900 rounded hover:bg-gray-100"
          >
            발행된 글
          </Link>
        </nav>
      </div>
      {userId && (
        <div className="flex items-center gap-3">
          <span className="text-sm text-gray-600">{userId}</span>
          <button
            onClick={onLogout}
            className="px-3 py-1 text-sm text-gray-500 hover:text-gray-700 rounded hover:bg-gray-100"
          >
            로그아웃
          </button>
        </div>
      )}
    </header>
  );
}
