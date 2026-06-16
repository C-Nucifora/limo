#include "ipcserver.h"
#include <QDebug>
#include <QLocalSocket>


IpcServer::IpcServer() : server_(new QLocalServer())
{
  // Security: restrict the IPC socket to the current user so other local users cannot inject
  // nxm:// download requests. The nxm:// desktop handler delivers links to this user's instance.
  server_->setSocketOptions(QLocalServer::UserAccessOption);
}

IpcServer::~IpcServer()
{
  server_->close();
  delete server_;
}

bool IpcServer::setup()
{
  bool started = server_->listen(server_name);
  if(!started)
  {
    // A stale socket left behind by a crashed previous instance prevents listen() from
    // succeeding. Remove it and retry so nxm:// handling does not silently break.
    QLocalServer::removeServer(server_name);
    started = server_->listen(server_name);
    if(!started)
      qWarning() << "IpcServer: failed to start IPC server:" << server_->errorString();
  }
  connect(server_, &QLocalServer::newConnection, this, &IpcServer::setupConnection);

  return started;
}

void IpcServer::shutdown()
{
  server_->close();
}

void IpcServer::setupConnection()
{
  auto socket = server_->nextPendingConnection();
  if(!socket)
    return;
  buffers_.insert(socket, QByteArray());
  connect(socket, &QLocalSocket::readyRead, this, &IpcServer::processData);
  // The client writes a single message then closes the connection. Buffer the bytes as they
  // arrive (a message may be split across multiple reads) and only emit it once the peer
  // disconnects, so partial reads no longer silently drop or fragment requests.
  connect(socket, &QLocalSocket::disconnected, this, &IpcServer::finalizeConnection);
}

void IpcServer::processData()
{
  auto socket = static_cast<QLocalSocket*>(sender());
  if(!socket)
    return;
  auto it = buffers_.find(socket);
  if(it == buffers_.end())
    return;
  it->append(socket->readAll());
  // Guard against a malicious or buggy peer streaming unbounded data into our buffer.
  if(it->size() > max_message_size)
    it->truncate(max_message_size);
}

void IpcServer::finalizeConnection()
{
  auto socket = static_cast<QLocalSocket*>(sender());
  if(!socket)
    return;
  auto it = buffers_.find(socket);
  if(it != buffers_.end())
  {
    // Drain anything still pending before emitting the reassembled message.
    it->append(socket->readAll());
    if(it->size() > max_message_size)
      it->truncate(max_message_size);
    const QByteArray message = *it;
    buffers_.erase(it);
    if(!message.isEmpty())
      emit receivedMessage(QString::fromUtf8(message));
  }
  socket->deleteLater();
}
