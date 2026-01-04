import { useState, useEffect } from 'react';
import { IDeviceClient, BookMetadata, ConnectionType, SectionInfo } from '../api/index';

type BookWithCover = BookMetadata & { coverUrl: string };

import EpubToImages from './EpubToImages';
import './BookShelf.css';

interface BookShelfProps {
  client: IDeviceClient;
  connectionType: ConnectionType;
  onDisconnect: () => void;
}

export default function BookShelf({ client, connectionType, onDisconnect }: BookShelfProps) {
  const [books, setBooks] = useState<BookWithCover[]>([]);
  const [loading, setLoading] = useState(false);
  const [selectedEpub, setSelectedEpub] = useState<File | null>(null);
  const [deviceInfo, setDeviceInfo] = useState<any>(null);

  // 加载设备信息和图书列表
  useEffect(() => {
    const init = async () => {
      try {
        const info = await client.getDeviceInfo();
        setDeviceInfo(info);
        await loadBooks();
      } catch (error) {
        console.error('Failed to initialize:', error);
      }
    };
    init();
  }, [client]);

  // 加载图书列表
  const loadBooks = async () => {
    setLoading(true);
    try {
      const bookList = await client.getBookList();
      // 添加 coverUrl 属性
      const booksWithCover: BookWithCover[] = bookList.map(book => ({
        ...book,
        coverUrl: book.coverUrl || `http://${(client as any).baseUrl?.replace('http://', '') || 'localhost'}/books/${book.id}/cover.png`
      }));
      setBooks(booksWithCover);
    } catch (error) {
      console.error('Failed to load books:', error);
    } finally {
      setLoading(false);
    }
  };

  // 删除图书
  const deleteBook = async (bookId: string, title: string) => {
    if (!confirm(`确定要删除《${title}》吗？此操作不可恢复。`)) {
      return;
    }

    try {
      await client.deleteBook(bookId);
      alert('删除成功！');
      await loadBooks();
    } catch (error) {
      alert('删除失败：' + error);
    }
  };

  // 选择 EPUB 文件
  const handleFileSelect = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (file && file.name.endsWith('.epub')) {
      setSelectedEpub(file);
    } else {
      alert('请选择 EPUB 格式的电子书');
    }
  };

  // EPUB 转换完成回调
  const handleConversionComplete = () => {
    setSelectedEpub(null);
    loadBooks();
  };

  // 如果正在处理 EPUB，显示转换界面
  if (selectedEpub) {
    return (
      <EpubToImages
        file={selectedEpub}
        onClose={() => setSelectedEpub(null)}
        onComplete={handleConversionComplete}
        client={client}
      />
    );
  }

  // 连接类型显示
  const connectionLabel = connectionType === 'http' ? '📶 WiFi' : '🔌 USB';

  return (
    <div className="bookshelf">
      {/* 头部 */}
      <header className="bookshelf-header">
        <h1>📚 M5PaperS3 书架</h1>
        
        <div className="device-status">
          <div className="device-info">
            <span className="status-indicator">●</span>
            <span className="connection-type">{connectionLabel}</span>
            {deviceInfo?.ip && <span>{deviceInfo.ip}</span>}
            <span className="storage">
              剩余: {((deviceInfo?.storage?.free || 0) / 1024 / 1024 / 1024).toFixed(2)} GB
            </span>
          </div>
          <button onClick={onDisconnect} className="disconnect-btn">断开</button>
        </div>
      </header>

      {/* 工具栏 */}
      <div className="toolbar">
        <label className="add-book-btn">
          <input
            type="file"
            accept=".epub"
            onChange={handleFileSelect}
            style={{ display: 'none' }}
          />
          ➕ 导入图书
        </label>
        <button onClick={loadBooks} disabled={loading}>
          🔄 刷新
        </button>
      </div>

      {/* 图书列表 */}
      <div className="book-grid">
        {loading && books.length === 0 ? (
          <div className="loading">加载中...</div>
        ) : books.length === 0 ? (
          <div className="empty-state">
            <p>📖 书架空空如也</p>
            <p>点击"导入图书"添加你的第一本电子书</p>
          </div>
        ) : (
          books.map((book) => (
            <div key={book.id} className="book-card">
              <div className="book-cover">
                <img
                  src={book.coverUrl}
                  alt={book.title}
                  onError={(e) => {
                    (e.target as HTMLImageElement).src = 'data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" width="200" height="200"><rect width="200" height="200" fill="%23ddd"/><text x="50%" y="50%" text-anchor="middle" dy=".3em" fill="%23999" font-size="20">📖</text></svg>';
                  }}
                />
              </div>
              <div className="book-info">
                <h3 className="book-title" title={book.title}>{book.title}</h3>
                {book.author && <p className="book-author">{book.author}</p>}
                <p className="book-meta">
                  {book.sections?.length || 0} 章节 / {
                    book.sections?.reduce((sum: number, s: SectionInfo) => sum + (s.pageCount || 0), 0) || 0
                  } 页
                </p>
              </div>
              <div className="book-actions">
                <button
                  className="delete-btn"
                  onClick={() => deleteBook(book.id, book.title)}
                >
                  🗑️ 删除
                </button>
              </div>
            </div>
          ))
        )}
      </div>
    </div>
  );
}
