import { useState } from 'react';
import { ConnectionPanel } from './components/ConnectionPanel';
import { LiveView } from './components/LiveView';
import { WidgetTreePanel } from './components/WidgetTree';
import { PropertyPanel } from './components/PropertyPanel';
import { TestEditor } from './components/TestEditor';
import { VisualDiffViewer } from './components/VisualDiffViewer';
import { SplitPane } from './components/SplitPane';
import { useHealthPoller } from './api/useHealthPoller';

type BottomTab = 'script' | 'visual';

function App() {
  useHealthPoller();
  const [bottomTab, setBottomTab] = useState<BottomTab>('script');

  return (
    <SplitPane
      direction="horizontal"
      defaultSize={288}
      minSize={200}
      maxSize={500}
      className="h-screen bg-slate-800 text-slate-200"
    >
      {/* Left sidebar */}
      <div className="h-full flex flex-col bg-slate-800">
        <div className="p-3 border-b border-slate-700">
          <h1 className="text-lg font-bold text-white">LVV</h1>
          <p className="text-xs text-slate-400">LVGL Test Automation</p>
        </div>
        <ConnectionPanel />
        <SplitPane
          direction="vertical"
          defaultSize={400}
          minSize={100}
          className="flex-1"
        >
          <div className="h-full overflow-hidden">
            <WidgetTreePanel />
          </div>
          <div className="h-full overflow-auto">
            <PropertyPanel />
          </div>
        </SplitPane>
      </div>

      {/* Main content */}
      <SplitPane
        direction="vertical"
        defaultSize={400}
        minSize={150}
        className="h-full"
      >
        {/* Top: Live view */}
        <div className="h-full min-h-0">
          <LiveView />
        </div>

        {/* Bottom panel with tabs */}
        <div className="h-full flex flex-col">
          <div className="flex border-b border-slate-700">
            <button
              onClick={() => setBottomTab('script')}
              className={`px-4 py-1.5 text-xs font-medium ${
                bottomTab === 'script'
                  ? 'bg-slate-700 text-white border-b-2 border-blue-400'
                  : 'text-slate-400 hover:text-white'
              }`}
            >
              Test Script
            </button>
            <button
              onClick={() => setBottomTab('visual')}
              className={`px-4 py-1.5 text-xs font-medium ${
                bottomTab === 'visual'
                  ? 'bg-slate-700 text-white border-b-2 border-blue-400'
                  : 'text-slate-400 hover:text-white'
              }`}
            >
              Visual Regression
            </button>
          </div>
          <div className="flex-1 overflow-hidden">
            {bottomTab === 'script' && <TestEditor />}
            {bottomTab === 'visual' && <VisualDiffViewer />}
          </div>
        </div>
      </SplitPane>
    </SplitPane>
  );
}

export default App;
