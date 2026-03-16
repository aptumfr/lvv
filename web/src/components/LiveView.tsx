import { useRef, useEffect, useState, useCallback } from 'react';
import { useWebSocket } from '../api/useWebSocket';
import { useAppStore } from '../store/appStore';
import { api } from '../api/client';

interface Point {
  x: number;
  y: number;
}

export function LiveView() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [streaming, setStreaming] = useState(false);
  const ws = useWebSocket('/ws', () => setStreaming(false));
  const { connected, recording, addRecordedStep, setSelectedWidget } = useAppStore();
  const canInteract = connected && ws.connected;
  const [hoverInfo, setHoverInfo] = useState<string | null>(null);
  const [dragStart, setDragStart] = useState<Point | null>(null);
  const [dragCurrent, setDragCurrent] = useState<Point | null>(null);

  // Draw frames from WebSocket with sequencing to prevent out-of-order rendering
  const frameSeq = useRef(0);
  useEffect(() => {
    if (!ws.lastFrame || !canvasRef.current) return;
    const seq = ++frameSeq.current;
    const img = new Image();
    img.onload = () => {
      if (seq !== frameSeq.current) return; // stale frame, drop it
      const canvas = canvasRef.current!;
      canvas.width = img.width;
      canvas.height = img.height;
      const ctx = canvas.getContext('2d')!;
      ctx.drawImage(img, 0, 0);
    };
    img.src = ws.lastFrame;
  }, [ws.lastFrame]);

  // Gray-fill the canvas when target disconnects
  useEffect(() => {
    if (connected) return;
    setStreaming(false);
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.fillStyle = '#334155'; // slate-700
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = '#94a3b8'; // slate-400
    ctx.font = '14px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('Disconnected', canvas.width / 2, canvas.height / 2);
  }, [connected]);

  const getCanvasCoords = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    const canvas = canvasRef.current;
    if (!canvas) return null;
    const rect = canvas.getBoundingClientRect();
    const scaleX = canvas.width / rect.width;
    const scaleY = canvas.height / rect.height;
    return {
      x: Math.round((e.clientX - rect.left) * scaleX),
      y: Math.round((e.clientY - rect.top) * scaleY),
    };
  }, []);

  const toggleStream = () => {
    if (streaming) {
      ws.stopStream();
      setStreaming(false);
    } else {
      ws.startStream(15);
      setStreaming(true);
    }
  };

  const takeScreenshot = () => {
    window.open(api.screenshotUrl(), '_blank');
  };

  const handleMouseDown = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const coords = getCanvasCoords(e);
    if (!coords || !canInteract) return;

    setDragStart(coords);
    setDragCurrent(coords);
    ws.press(coords.x, coords.y);
  };

  const handleMouseUp = async (e: React.MouseEvent<HTMLCanvasElement>) => {
    const coords = getCanvasCoords(e);
    if (!coords || !canInteract) {
      setDragStart(null);
      setDragCurrent(null);
      return;
    }

    const start = dragStart;
    if (!start) return;

    ws.moveTo(coords.x, coords.y);
    ws.release();

    setDragStart(null);
    setDragCurrent(null);

    const dx = Math.abs(coords.x - start.x);
    const dy = Math.abs(coords.y - start.y);
    const wasDrag = dx > 10 || dy > 10;

    if (recording) {
      if (wasDrag) {
        addRecordedStep(`lvv.swipe(${start.x}, ${start.y}, ${coords.x}, ${coords.y}, 300)`);
        return;
      }

      try {
        const result = await api.findAt(coords.x, coords.y);
        if (result.found && result.selector) {
          addRecordedStep(`lvv.click("${result.selector}")`);
          if (result.name || result.auto_path) {
            setSelectedWidget({
              id: 0,
              name: result.name || '',
              type: result.type || '',
              x: result.x || 0,
              y: result.y || 0,
              width: result.width || 0,
              height: result.height || 0,
              visible: true,
              clickable: true,
              auto_path: result.auto_path || '',
              text: result.text || '',
            });
          }
        } else {
          addRecordedStep(`lvv.click_at(${coords.x}, ${coords.y})`);
        }
      } catch {
        addRecordedStep(`lvv.click_at(${coords.x}, ${coords.y})`);
      }
    }
  };

  const handleMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
    if (!canInteract) return;
    const coords = getCanvasCoords(e);
    if (!coords) return;
    setHoverInfo(`${coords.x}, ${coords.y}`);

    if (!dragStart) return;

    setDragCurrent(coords);
    ws.moveTo(coords.x, coords.y);
  };

  const handleMouseLeave = () => {
    setHoverInfo(null);

    if (!dragStart) return;

    if (dragCurrent) {
      ws.moveTo(dragCurrent.x, dragCurrent.y);
    }
    ws.release();

    setDragStart(null);
    setDragCurrent(null);
  };

  return (
    <div className="flex flex-col h-full">
      <div className="flex items-center gap-2 p-2 border-b border-slate-700">
        <h2 className="text-sm font-semibold">Live View</h2>
        <button
          onClick={toggleStream}
          disabled={!canInteract}
          className={`px-2 py-1 text-xs rounded ${
            streaming ? 'bg-red-600 hover:bg-red-500' : 'bg-green-600 hover:bg-green-500'
          } disabled:opacity-50`}
        >
          {streaming ? 'Stop' : 'Stream'}
        </button>
        <button
          onClick={takeScreenshot}
          disabled={!canInteract}
          className="px-2 py-1 text-xs bg-slate-600 hover:bg-slate-500 rounded disabled:opacity-50"
        >
          Screenshot
        </button>
        {hoverInfo && (
          <span className="text-xs text-slate-400 ml-auto font-mono">{hoverInfo}</span>
        )}
      </div>
      <div className="flex-1 overflow-auto p-2 flex items-center justify-center bg-slate-900">
        <canvas
          ref={canvasRef}
          className="max-w-full max-h-full object-contain border border-slate-600 cursor-crosshair"
          onMouseDown={handleMouseDown}
          onMouseUp={handleMouseUp}
          onMouseMove={handleMouseMove}
          onMouseLeave={handleMouseLeave}
          width={480}
          height={320}
        />
      </div>
    </div>
  );
}
