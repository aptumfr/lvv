import { useAppStore } from '../store/appStore';

export function PropertyPanel() {
  const { selectedWidget } = useAppStore();

  if (!selectedWidget) {
    return (
      <div className="p-3 text-slate-500 text-xs">
        Select a widget to view properties
      </div>
    );
  }

  const props = [
    ['Type', selectedWidget.type],
    ['Name', selectedWidget.name || '(none)'],
    ['Auto Path', selectedWidget.auto_path || '(none)'],
    ['Text', selectedWidget.text || '(none)'],
    ['Position', `${selectedWidget.x}, ${selectedWidget.y}`],
    ['Size', `${selectedWidget.width} x ${selectedWidget.height}`],
    ['Visible', selectedWidget.visible ? 'Yes' : 'No'],
    ['Clickable', selectedWidget.clickable ? 'Yes' : 'No'],
  ];

  return (
    <div className="p-2">
      <h2 className="text-sm font-semibold mb-2 px-1">Properties</h2>
      <table className="w-full text-xs">
        <tbody>
          {props.map(([key, val]) => (
            <tr key={key} className="border-b border-slate-700/50">
              <td className="py-1 px-1 text-slate-400 font-medium">{key}</td>
              <td className="py-1 px-1 text-slate-200 font-mono">{val}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
