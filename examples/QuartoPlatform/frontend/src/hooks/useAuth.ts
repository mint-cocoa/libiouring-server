import { useState, useEffect } from "react";
import { api } from "../api/client";

interface User {
  user_id: string;
  github_id: number;
}

export function useAuth() {
  const [user, setUser] = useState<User | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    if (import.meta.env.VITE_DEV_USER) {
      setUser({ user_id: import.meta.env.VITE_DEV_USER, github_id: 0 });
      setLoading(false);
      return;
    }
    api<User>("/auth/me")
      .then(setUser)
      .catch(() => setUser(null))
      .finally(() => setLoading(false));
  }, []);

  const login = () => {
    window.location.href = "/auth/github";
  };

  const logout = async () => {
    await fetch("/auth/logout", { method: "POST", credentials: "include" });
    setUser(null);
    window.location.href = "/login";
  };

  return { user, loading, login, logout };
}
