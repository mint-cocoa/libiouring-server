import { useState, useCallback } from "react";
import { api } from "../api/client";

interface PublishedItem {
  slug: string;
  url_path: string;
}

export function usePublish() {
  const [published, setPublished] = useState<PublishedItem[]>([]);
  const [loading, setLoading] = useState(false);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const res = await api<{ published: PublishedItem[] }>("/publish");
      setPublished(res.published);
    } finally {
      setLoading(false);
    }
  }, []);

  const publish = async (slug: string): Promise<string> => {
    const res = await api<{ status: string; slug: string; url_path: string }>(
      `/publish/${slug}`,
      { method: "POST" },
    );
    await refresh();
    return res.url_path;
  };

  const unpublish = async (slug: string) => {
    await api(`/publish/${slug}`, { method: "DELETE" });
    await refresh();
  };

  return { published, loading, refresh, publish, unpublish };
}
