"""Bolt 服务就绪探测：完成一次 v4.4 握手并读回 4 字节响应。

判据说明：TCP connect 成功只代表端口在监听，此时 Bolt handler 可能尚未注册完，
客户端连接会被拒（表现为驱动侧 ServiceUnavailableException）。这里要求服务端
**实际应答握手**，才算就绪。
"""
import socket
import sys

port = int(sys.argv[1])
MAGIC = bytes([0x60, 0x60, 0xB0, 0x17])
# 提议 4.4 / 4.3 / 4.2 / 3.0（服务端挑一个）
VERSIONS = bytes([0, 0, 4, 4, 0, 0, 4, 3, 0, 0, 4, 2, 0, 0, 0, 3])

s = socket.socket()
s.settimeout(2.0)
try:
    s.connect(("127.0.0.1", port))
    s.sendall(MAGIC + VERSIONS)
    resp = s.recv(4)
    # 服务端回 4 字节选定的版本；全 0 表示不支持，但能回就说明处理逻辑已就绪
    sys.exit(0 if len(resp) == 4 else 1)
except Exception:
    sys.exit(1)
finally:
    s.close()
