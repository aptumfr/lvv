import { useEffect, useRef, useCallback, useState } from 'react';

export function useWebSocket(url: string, onDisconnect?: () => void) {
  const wsRef = useRef<WebSocket | null>(null);
  const [connected, setConnected] = useState(false);
  const [lastFrame, setLastFrame] = useState<ImageBitmap | null>(null);
  const frameSeq = useRef(0);
  const reconnectTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const mountedRef = useRef(true);
  const onDisconnectRef = useRef(onDisconnect);
  onDisconnectRef.current = onDisconnect;

  const connectWs = useCallback(() => {
    if (!mountedRef.current) return;

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}${url}`;
    const ws = new WebSocket(wsUrl);
    ws.binaryType = 'blob';

    ws.onopen = () => setConnected(true);
    ws.onclose = () => {
      setConnected(false);
      wsRef.current = null;
      onDisconnectRef.current?.();
      if (mountedRef.current) {
        reconnectTimer.current = setTimeout(connectWs, 2000);
      }
    };

    ws.onmessage = (event) => {
      if (event.data instanceof Blob) {
        // Binary message = JPEG frame — decode off main thread
        const seq = ++frameSeq.current;
        createImageBitmap(event.data).then(bitmap => {
          if (seq !== frameSeq.current) { bitmap.close(); return; } // stale
          setLastFrame(prev => { prev?.close(); return bitmap; });
        }).catch(() => {});
      }
    };

    wsRef.current = ws;
  }, [url]);

  useEffect(() => {
    mountedRef.current = true;
    connectWs();
    return () => {
      mountedRef.current = false;
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current);
      wsRef.current?.close();
    };
  }, [connectWs]);

  const send = useCallback((msg: Record<string, unknown>) => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify(msg));
    }
  }, []);

  // --- Streaming ---
  const startStream = useCallback((fps = 10) => {
    send({ type: 'start_stream', fps });
  }, [send]);

  const stopStream = useCallback(() => {
    send({ type: 'stop_stream' });
  }, [send]);

  // --- Interaction commands (fire-and-forget) ---
  const press = useCallback((x: number, y: number) => {
    send({ type: 'press', x, y });
  }, [send]);

  const release = useCallback(() => {
    send({ type: 'release' });
  }, [send]);

  const moveTo = useCallback((x: number, y: number) => {
    send({ type: 'move_to', x, y });
  }, [send]);

  const clickAt = useCallback((x: number, y: number) => {
    send({ type: 'click_at', x, y });
  }, [send]);

  const click = useCallback((name: string) => {
    send({ type: 'click', name });
  }, [send]);

  const swipe = useCallback((x: number, y: number, x_end: number, y_end: number, duration: number) => {
    send({ type: 'swipe', x, y, x_end, y_end, duration });
  }, [send]);

  const typeText = useCallback((text: string) => {
    send({ type: 'type', text });
  }, [send]);

  const sendKey = useCallback((key: string) => {
    send({ type: 'key', key });
  }, [send]);

  return {
    connected, lastFrame,
    startStream, stopStream,
    press, release, moveTo, clickAt, click, swipe, typeText, sendKey,
  };
}
