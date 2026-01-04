import { useState, useEffect } from 'react';
import BookShelf from './components/BookShelf';
import { IDeviceClient, getHttpClient, ConnectionType } from './api/index';
import './App.css';

function App() {
  const [connectionType, setConnectionType] = useState<ConnectionType | null>(null);
  const [client, setClient] = useState<IDeviceClient | null>(null);
  const [connecting, setConnecting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  
  // HTTP 连接状态
  const [deviceIp, setDeviceIp] = useState(localStorage.getItem('device-ip') || '');

  // 自动恢复上次的连接方式
  useEffect(() => {
    const savedType = localStorage.getItem('connection-type') as ConnectionType | null;
    if (savedType === 'http' && deviceIp) {
      handleHttpConnect();
    }
  }, []);

  // HTTP 连接
  const handleHttpConnect = async () => {
    if (!deviceIp.trim()) {
      setError('请输入设备 IP 地址');
      return;
    }

    setConnecting(true);
    setError(null);
    
    try {
      const httpClient = getHttpClient();
      httpClient.setDevice(deviceIp.trim());
      
      const success = await httpClient.testConnection();
      if (success) {
        localStorage.setItem('device-ip', deviceIp.trim());
        localStorage.setItem('connection-type', 'http');
        setConnectionType('http');
        setClient(httpClient);
      } else {
        setError('连接失败，请检查 IP 地址和网络连接');
      }
    } catch (err) {
      setError('连接失败：' + (err instanceof Error ? err.message : String(err)));
    } finally {
      setConnecting(false);
    }
  };

  // 断开连接
  const handleDisconnect = () => {
    setClient(null);
    setConnectionType(null);
    localStorage.removeItem('connection-type');
  };

  // 已连接，显示书架
  if (client && connectionType) {
    return (
      <div className="app">
        <BookShelf 
          client={client} 
          connectionType={connectionType}
          onDisconnect={handleDisconnect}
        />
      </div>
    );
  }

  // 连接选择界面
  return (
    <div className="app">
      <div className="connection-selector">
        <h1>📚 M5PaperS3 书架</h1>
        <p className="subtitle">连接您的设备</p>

        {error && (
          <div className="error-message">
            ⚠️ {error}
          </div>
        )}

        <div className="connection-options">
          {/* WiFi 连接 */}
          <div className="connection-card">
            <div className="connection-icon">📶</div>
            <h2>WiFi 连接</h2>
            <p className="connection-desc">通过 WiFi 网络连接设备</p>
            
            <div className="connection-form">
              <input
                type="text"
                placeholder="设备 IP 地址 (例: 192.168.1.100)"
                value={deviceIp}
                onChange={(e) => setDeviceIp(e.target.value)}
                onKeyPress={(e) => e.key === 'Enter' && handleHttpConnect()}
                disabled={connecting}
              />
              <button 
                onClick={handleHttpConnect} 
                disabled={connecting || !deviceIp.trim()}
                className="connect-btn"
              >
                {connecting ? '连接中...' : '连接'}
              </button>
            </div>
          </div>
        </div>

        <div className="connection-help">
          <h3>💡 如何连接？</h3>
          <div className="help-item">
            <ol>
              <li>确保设备已连接 WiFi</li>
              <li>在设备屏幕查看 IP 地址</li>
              <li>输入 IP 地址并点击连接</li>
            </ol>
          </div>
        </div>
      </div>
    </div>
  );
}

export default App;

