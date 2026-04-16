import { useEffect, useState } from "react";

interface ToastProps {
  message: string | null;
  onClose: () => void;
}

export function Toast({ message, onClose }: ToastProps) {
  const [visible, setVisible] = useState(false);

  useEffect(() => {
    if (message) {
      setVisible(true);
      const timer = setTimeout(() => {
        setVisible(false);
        onClose();
      }, 3000);
      return () => clearTimeout(timer);
    }
  }, [message, onClose]);

  if (!visible || !message) return null;

  return (
    <div className="fixed bottom-4 right-4 px-4 py-2 bg-gray-900 text-white text-sm rounded-lg shadow-lg">
      {message}
    </div>
  );
}
