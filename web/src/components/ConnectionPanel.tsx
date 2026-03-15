import { useState } from 'react';
import { api } from '../api/client';
import { useAppStore } from '../store/appStore';

export function ConnectionPanel() {
  const { connected, setConnected } = useAppStore();
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  const handleConnect = async () => {
    setLoading(true);
    setError('');
    try {
      const res = await api.connect();
      setConnected(res.connected);
      if (!res.connected) setError('Connection failed');
    } catch (e: any) {
      setError(e.message);
    }
    setLoading(false);
  };

  const handleDisconnect = async () => {
    try {
      await api.disconnect();
      setConnected(false);
    } catch (e: any) {
      setError(e.message);
    }
  };

  return (
    <div className="p-3 border-b border-slate-700">
      <div className="flex items-center gap-2 mb-2">
        <div className={`w-2 h-2 rounded-full ${connected ? 'bg-green-400' : 'bg-red-400'}`} />
        <span className="text-sm font-medium">
          {connected ? 'Connected' : 'Disconnected'}
        </span>
      </div>
      <div className="flex gap-2">
        {!connected ? (
          <button
            onClick={handleConnect}
            disabled={loading}
            className="px-3 py-1 text-sm bg-blue-600 hover:bg-blue-500 rounded disabled:opacity-50"
          >
            {loading ? 'Connecting...' : 'Connect'}
          </button>
        ) : (
          <button
            onClick={handleDisconnect}
            className="px-3 py-1 text-sm bg-slate-600 hover:bg-slate-500 rounded"
          >
            Disconnect
          </button>
        )}
      </div>
      {error && <p className="text-red-400 text-xs mt-1">{error}</p>}
    </div>
  );
}
