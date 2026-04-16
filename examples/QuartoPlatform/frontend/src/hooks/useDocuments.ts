import { useState, useEffect, useCallback } from "react";
import { api } from "../api/client";

interface DocMeta {
  slug: string;
  title: string;
  date: string;
}

interface DocContent {
  slug: string;
  content: string;
}

export function useDocuments() {
  const [documents, setDocuments] = useState<DocMeta[]>([]);
  const [loading, setLoading] = useState(true);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const res = await api<{ documents: DocMeta[] }>("/documents");
      setDocuments(res.documents);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    refresh();
  }, [refresh]);

  const getDocument = async (slug: string): Promise<string> => {
    const res = await api<DocContent>(`/documents/${slug}`);
    return res.content;
  };

  const saveDocument = async (slug: string, content: string) => {
    await api(`/documents/${slug}`, {
      method: "POST",
      body: JSON.stringify({ content }),
    });
  };

  const deleteDocument = async (slug: string) => {
    await api(`/documents/${slug}`, { method: "DELETE" });
    await refresh();
  };

  return { documents, loading, refresh, getDocument, saveDocument, deleteDocument };
}
