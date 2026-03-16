import { useRef, useCallback, useEffect, useState } from 'react';

type Direction = 'horizontal' | 'vertical';

interface SplitPaneProps {
  direction: Direction;
  defaultSize: number;       // initial size in pixels of the first pane
  minSize?: number;           // min size of first pane
  maxSize?: number;           // max size of first pane
  children: [React.ReactNode, React.ReactNode];
  className?: string;
}

export function SplitPane({
  direction,
  defaultSize,
  minSize = 100,
  maxSize = Infinity,
  children,
  className = '',
}: SplitPaneProps) {
  const [size, setSize] = useState(defaultSize);
  const containerRef = useRef<HTMLDivElement>(null);
  const dragging = useRef(false);

  const isHorizontal = direction === 'horizontal';

  const onMouseDown = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    dragging.current = true;
    document.body.style.cursor = isHorizontal ? 'col-resize' : 'row-resize';
    document.body.style.userSelect = 'none';
  }, [isHorizontal]);

  useEffect(() => {
    const onMouseMove = (e: MouseEvent) => {
      if (!dragging.current || !containerRef.current) return;
      const rect = containerRef.current.getBoundingClientRect();
      let newSize: number;
      if (isHorizontal) {
        newSize = e.clientX - rect.left;
      } else {
        newSize = e.clientY - rect.top;
      }
      const containerSize = isHorizontal ? rect.width : rect.height;
      const clampedMax = Math.min(maxSize, containerSize - minSize);
      newSize = Math.max(minSize, Math.min(clampedMax, newSize));
      setSize(newSize);
    };

    const onMouseUp = () => {
      if (dragging.current) {
        dragging.current = false;
        document.body.style.cursor = '';
        document.body.style.userSelect = '';
      }
    };

    document.addEventListener('mousemove', onMouseMove);
    document.addEventListener('mouseup', onMouseUp);
    return () => {
      document.removeEventListener('mousemove', onMouseMove);
      document.removeEventListener('mouseup', onMouseUp);
    };
  }, [isHorizontal, minSize, maxSize]);

  const dividerClass = isHorizontal
    ? 'w-1 cursor-col-resize hover:bg-blue-500 active:bg-blue-400 bg-slate-600 flex-shrink-0'
    : 'h-1 cursor-row-resize hover:bg-blue-500 active:bg-blue-400 bg-slate-600 flex-shrink-0';

  return (
    <div
      ref={containerRef}
      className={`flex ${isHorizontal ? 'flex-row' : 'flex-col'} ${className}`}
    >
      <div
        style={isHorizontal ? { width: size } : { height: size }}
        className={`flex-shrink-0 overflow-hidden ${isHorizontal ? 'min-w-0' : 'min-h-0'}`}
      >
        {children[0]}
      </div>
      <div className={dividerClass} onMouseDown={onMouseDown} />
      <div className="flex-1 min-w-0 min-h-0 overflow-hidden">
        {children[1]}
      </div>
    </div>
  );
}
