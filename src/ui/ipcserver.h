/*!
 * \file ipcserver.h
 * \brief Header for the IpcServer class.
 */

#pragma once

#include <QByteArray>
#include <QHash>
#include <QLocalServer>

class QLocalSocket;


/*!
 * \brief Manages a QLocalServer used for communication with other Limo instances.
 */
class IpcServer : public QObject
{
  Q_OBJECT
public:
  /*! \brief Initializes the server. Does NOT start it. */
  IpcServer();
  /*! \brief Stops and deletes the server. */
  ~IpcServer();

  /*! \brief The name of the server. */
  static constexpr char server_name[] = "_Limo_Server_";

  /*!
   * \brief Starts the server.
   * \return True if the server is running.
   */
  bool setup();
  /*! \brief Stops the server. */
  void shutdown();

private:
  /*! \brief The server used for IPC. */
  QLocalServer* server_;
  /*!
   * \brief Per-socket receive buffers used to reassemble messages that may arrive across
   * multiple reads. The complete message is emitted once the peer disconnects.
   */
  QHash<QLocalSocket*, QByteArray> buffers_;
  /*! \brief Upper bound on a single buffered message to prevent unbounded memory growth. */
  static constexpr qsizetype max_message_size = 64 * 1024;

private slots:
  /*! \brief Initializes a connection with a QLocalSocket. */
  void setupConnection();
  /*! \brief Processes data received from a QLocalSocket. */
  void processData();
  /*! \brief Emits the fully reassembled message and cleans up after a socket disconnects. */
  void finalizeConnection();

signals:
  /*!
   * \brief Sends the message received from an IpcClient.
   * \param message The message.
   */
  void receivedMessage(QString message);
};
