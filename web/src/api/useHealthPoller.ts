import { useEffect, useRef } from 'react';
import { api } from './client';
import { useAppStore } from '../store/appStore';

const POLL_INTERVAL_MS = 3000;

/**
 * Polls /api/health to keep the store's `connected` flag in sync
 * with the actual target state. Detects unexpected disconnects.
 */
export function useHealthPoller() {
  const setConnected = useAppStore((s) => s.setConnected);
  const timerRef = useRef<ReturnType<typeof setInterval> | null>(null);

  useEffect(() => {
    const poll = async () => {
      try {
        const res = await api.health();
        setConnected(res.connected);
      } catch {
        // Server unreachable — mark disconnected
        setConnected(false);
      }
    };

    // Initial check
    poll();
    timerRef.current = setInterval(poll, POLL_INTERVAL_MS);

    return () => {
      if (timerRef.current) clearInterval(timerRef.current);
    };
  }, [setConnected]);
}
