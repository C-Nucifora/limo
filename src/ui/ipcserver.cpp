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
  connect(socket, &QLocalSocket::readyRead, this, &IpcServer::processData);
}

void IpcServer::processData()
{
  auto socket = static_cast<QLocalSocket*>(sender());
  auto bytes = socket->readAll();
  std::string data;
  for(const auto& b : std::as_const(bytes))
    data.append({ b });
  emit receivedMessage(data.c_str());
}
