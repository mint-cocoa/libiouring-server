import { useRef, useCallback, useState } from "react";

type SaveStatus = "idle" | "saving" | "saved" | "error";

export function useAutoSave(
  saveFn: (content: string) => Promise<void>,
  delayMs: number = 500,
) {
  const [status, setStatus] = useState<SaveStatus>("idle");
  const [lastSaved, setLastSaved] = useState<Date | null>(null);
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const trigger = useCallback(
    (content: string) => {
      if (timerRef.current) {
        clearTimeout(timerRef.current);
      }
      timerRef.current = setTimeout(async () => {
        setStatus("saving");
        try {
          await saveFn(content);
          setStatus("saved");
          setLastSaved(new Date());
        } catch {
          setStatus("error");
        }
      }, delayMs);
    },
    [saveFn, delayMs],
  );

  return { trigger, status, lastSaved };
}
