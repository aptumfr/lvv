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
  const dragStartTime = useRef<number>(0);
  const lastActionTime = useRef<number>(0);
  const hoverThrottle = useRef<number>(0);

  // Reset action timer when recording starts
  useEffect(() => {
    if (recording) lastActionTime.current = 0;
  }, [recording]);

  // Draw frames — ImageBitmap already decoded off main thread, sequenced in useWebSocket
  useEffect(() => {
    if (!ws.lastFrame || !canvasRef.current) return;
    const canvas = canvasRef.current;
    canvas.width = ws.lastFrame.width;
    canvas.height = ws.lastFrame.height;
    const ctx = canvas.getContext('2d')!;
    ctx.drawImage(ws.lastFrame, 0, 0);
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
    dragStartTime.current = Date.now();
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
    const elapsed = Date.now() - dragStartTime.current;

    if (recording) {
      // Record real elapsed time since last action (minimum 100ms)
      const now = Date.now();
      if (lastActionTime.current > 0) {
        const gap = now - lastActionTime.current;
        const waitMs = Math.max(Math.round(gap / 100) * 100, 100);
        addRecordedStep(`lvv.wait(${waitMs})`);
      }
      lastActionTime.current = now;

      if (wasDrag) {
        const duration = Math.max(Math.round(elapsed), 200);
        // Try to find the widget at drag start for relative coordinates
        try {
          const dragWidget = await api.findAt(start.x, start.y);
          if (dragWidget.found && dragWidget.selector && dragWidget.name &&
              (dragWidget.type === 'slider' || dragWidget.type === 'obj')) {
            const fn = elapsed > 400 ? 'drag' : 'swipe';
            const relX1 = start.x - (dragWidget.x || 0);
            const relY1 = start.y - (dragWidget.y || 0);
            const relX2 = coords.x - (dragWidget.x || 0);
            const relY2 = coords.y - (dragWidget.y || 0);
            addRecordedStep(`x, y, w, h = lvv.widget_coords("${dragWidget.selector}")`);
            addRecordedStep(`lvv.${fn}(x+${relX1}, y+${relY1}, x+${relX2}, y+${relY2}, ${duration})`);
            return;
          }
        } catch {}
        // Fallback: absolute coordinates
        if (elapsed > 400) {
          addRecordedStep(`lvv.drag(${start.x}, ${start.y}, ${coords.x}, ${coords.y}, ${duration})`);
        } else {
          addRecordedStep(`lvv.swipe(${start.x}, ${start.y}, ${coords.x}, ${coords.y}, ${duration})`);
        }
        return;
      }

      // Snapshot visible widgets before click to detect screen changes
      let beforeVisible: Set<string> | null = null;
      try {
        const widgets = await api.widgets();
        beforeVisible = new Set(
          widgets.filter(w => w.visible && w.name).map(w => w.name)
        );
      } catch {}

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

          // After clicking a button, check if a new screen/dialog appeared
          if (result.type === 'button' || result.clickable) {
            try {
              // Brief wait for LVGL to process the click
              await new Promise(r => setTimeout(r, 300));
              const afterWidgets = await api.widgets();
              const afterVisible = afterWidgets.filter(w => w.visible && w.name);

              // Find newly visible named widgets (screens, dialogs)
              for (const w of afterVisible) {
                if (beforeVisible && !beforeVisible.has(w.name) && w.name) {
                  // A new named widget appeared — emit wait_for
                  addRecordedStep(`lvv.wait_for("${w.name}", 2000)`);
                  lastActionTime.current = Date.now(); // reset timer after the 300ms probe
                  break;
                }
              }
            } catch {}
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

    // Throttle hover info updates to ~30fps
    const now = Date.now();
    if (now - hoverThrottle.current > 33) {
      hoverThrottle.current = now;
      setHoverInfo(`${coords.x}, ${coords.y}`);
    }

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
        {recording && (
          <span className="text-xs text-red-400 ml-2 animate-pulse">Recording</span>
        )}
        {hoverInfo && (
          <span className="text-xs text-slate-400 ml-auto font-mono">{hoverInfo}</span>
        )}
      </div>
      <div className="flex-1 overflow-auto p-2 flex items-center justify-center bg-slate-900">
        <canvas
          ref={canvasRef}
          className={`max-w-full max-h-full object-contain border cursor-crosshair ${
            recording ? 'border-red-500' : 'border-slate-600'
          }`}
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
