import { useState } from 'react';
import { api, type VisualDiffResult } from '../api/client';
import { useAppStore } from '../store/appStore';

export function VisualDiffViewer() {
  const { connected } = useAppStore();
  const [refPath, setRefPath] = useState('ref_images/test.png');
  const [threshold, setThreshold] = useState(0.1);
  const [result, setResult] = useState<VisualDiffResult | null>(null);
  const [running, setRunning] = useState(false);
  const [error, setError] = useState('');

  const handleCompare = async () => {
    if (!connected) return;
    setRunning(true);
    setError('');
    setResult(null);
    try {
      const res = await api.visualCompare(refPath, threshold);
      setResult(res);
    } catch (e: any) {
      setError(e.message);
    }
    setRunning(false);
  };

  return (
    <div className="p-3">
      <h2 className="text-sm font-semibold mb-2">Visual Regression</h2>

      <div className="flex gap-2 mb-2">
        <input
          type="text"
          value={refPath}
          onChange={(e) => setRefPath(e.target.value)}
          placeholder="Reference image path"
          className="flex-1 px-2 py-1 text-xs bg-slate-900 border border-slate-600 rounded text-slate-200 outline-none focus:border-blue-500"
        />
        <input
          type="number"
          value={threshold}
          onChange={(e) => {
            const v = parseFloat(e.target.value);
            setThreshold(Number.isFinite(v) ? Math.max(0, Math.min(100, v)) : 0.1);
          }}
          step={0.01}
          min={0}
          max={100}
          className="w-20 px-2 py-1 text-xs bg-slate-900 border border-slate-600 rounded text-slate-200 outline-none"
          title="Threshold %"
        />
        <button
          onClick={handleCompare}
          disabled={!connected || running}
          className="px-3 py-1 text-xs bg-blue-600 hover:bg-blue-500 rounded disabled:opacity-50"
        >
          {running ? '...' : 'Compare'}
        </button>
      </div>

      {error && <p className="text-red-400 text-xs mb-2">{error}</p>}

      {result && (
        <div className={`p-2 rounded text-xs ${result.passed ? 'bg-green-900/30 border border-green-700' : 'bg-red-900/30 border border-red-700'}`}>
          <div className="flex items-center gap-2 mb-1">
            <span className={`font-bold ${result.passed ? 'text-green-400' : 'text-red-400'}`}>
              {result.first_run ? 'REFERENCE CREATED' : result.passed ? 'PASSED' : 'FAILED'}
            </span>
          </div>
          {!result.first_run && (
            <div className="space-y-0.5 text-slate-300">
              <p>Diff: {result.diff_percentage.toFixed(4)}%</p>
              <p>Changed pixels: {result.diff_pixels.toLocaleString()} / {result.total_pixels.toLocaleString()}</p>
              {result.identical && <p className="text-green-400">Images are identical</p>}
            </div>
          )}
          {result.message && <p className="text-slate-400 mt-1">{result.message}</p>}
        </div>
      )}
    </div>
  );
}
