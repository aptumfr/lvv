import { useState, useEffect } from 'react';
import { api } from '../api/client';
import { useAppStore } from '../store/appStore';
import type { WidgetInfo } from '../types';

function TreeNode({ widget, depth = 0 }: { widget: WidgetInfo; depth?: number }) {
  const [expanded, setExpanded] = useState(depth < 2);
  const { setSelectedWidget, selectedWidget, recording, addRecordedStep } = useAppStore();
  const hasChildren = widget.children && widget.children.length > 0;
  const isSelected = selectedWidget?.auto_path === widget.auto_path && widget.auto_path !== '';

  const handleClick = () => {
    setSelectedWidget(widget);
    if (recording && widget.name) {
      addRecordedStep(`lvv.click("${widget.name}")`);
    }
  };

  return (
    <div>
      <div
        className={`flex items-center gap-1 px-2 py-0.5 cursor-pointer text-xs hover:bg-slate-700 ${
          isSelected ? 'bg-slate-600' : ''
        }`}
        style={{ paddingLeft: `${depth * 16 + 4}px` }}
        onClick={handleClick}
      >
        {hasChildren ? (
          <button
            onClick={(e) => { e.stopPropagation(); setExpanded(!expanded); }}
            className="w-4 text-slate-400 hover:text-white"
          >
            {expanded ? '▼' : '▶'}
          </button>
        ) : (
          <span className="w-4" />
        )}
        <span className="text-blue-400">{widget.type}</span>
        {widget.name && <span className="text-green-400 ml-1">#{widget.name}</span>}
        {widget.text && <span className="text-slate-400 ml-1 truncate max-w-32">"{widget.text}"</span>}
        {!widget.visible && <span className="text-red-400 ml-1 text-[10px]">[hidden]</span>}
      </div>
      {expanded && hasChildren && widget.children!.map((child, i) => (
        <TreeNode key={`${child.auto_path || i}`} widget={child} depth={depth + 1} />
      ))}
    </div>
  );
}

export function WidgetTreePanel() {
  const { connected, widgetTree, setWidgetTree } = useAppStore();
  const [loading, setLoading] = useState(false);

  const refresh = async () => {
    if (!connected) return;
    setLoading(true);
    try {
      const tree = await api.tree();
      setWidgetTree(tree);
    } catch {
      // ignore
    }
    setLoading(false);
  };

  useEffect(() => {
    if (connected) refresh();
  }, [connected]);

  return (
    <div className="flex flex-col h-full">
      <div className="flex items-center gap-2 p-2 border-b border-slate-700">
        <h2 className="text-sm font-semibold">Widget Tree</h2>
        <button
          onClick={refresh}
          disabled={!connected || loading}
          className="px-2 py-1 text-xs bg-slate-600 hover:bg-slate-500 rounded disabled:opacity-50"
        >
          {loading ? '...' : 'Refresh'}
        </button>
      </div>
      <div className="flex-1 overflow-auto font-mono">
        {widgetTree ? (
          <TreeNode widget={widgetTree} />
        ) : (
          <p className="text-slate-500 text-xs p-3">
            {connected ? 'Click Refresh to load tree' : 'Not connected'}
          </p>
        )}
      </div>
    </div>
  );
}
