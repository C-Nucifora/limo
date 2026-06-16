/*!
 * \file nexusnewsdialog.h
 * \brief Header for the NexusNewsDialog class
 */

// fork #211: in-app Nexus news/announcements feed

#pragma once

#include <QDialog>
#include <QString>
#include <vector>

namespace Ui
{
class NexusNewsDialog;
}

/*!
 * \brief Dialog used to display a lightweight Nexus/site news/announcements feed.
 *
 * Fetches an RSS feed over HTTP (off the GUI thread) and lists the headlines with
 * their publication dates. Double-clicking an item or pressing "Open in Browser"
 * opens the associated link in the system browser.
 */
class NexusNewsDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI and starts fetching the news feed.
   * \param parent Parent widget.
   */
  explicit NexusNewsDialog(QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~NexusNewsDialog();

private:
  /*! \brief A single news feed entry. */
  struct NewsItem
  {
    /*! \brief Headline title. */
    QString title;
    /*! \brief Link to the full article. */
    QString link;
    /*! \brief Publication date as provided by the feed. */
    QString pub_date;
  };

  /*! \brief Contains auto-generated UI elements. */
  Ui::NexusNewsDialog* ui;
  /*! \brief URL of the RSS news feed. */
  QString feed_url_;
  /*! \brief Items currently shown in the list. */
  std::vector<NewsItem> items_;
  /*! \brief Indicates that a fetch is currently in progress. */
  bool is_loading_ = false;

  /*! \brief Default Nexus Mods news RSS feed URL. */
  static constexpr const char* DEFAULT_FEED_URL = "https://www.nexusmods.com/rss/news/";

  /*!
   * \brief Parses an RSS feed body into news items.
   * \param body Raw RSS/XML feed body.
   * \return Parsed items. Empty if the body could not be parsed.
   */
  static std::vector<NewsItem> parseRss(const QString& body);
  /*! \brief Updates the list widget to reflect the current items. */
  void updateList();
  /*! \brief Opens the link of the given list row in the system browser. */
  void openRow(int row);

signals:
  /*!
   * \brief Emitted from the worker thread once a fetch completes.
   * \param success True if the request succeeded.
   * \param body Raw feed body on success, error message on failure.
   */
  void fetchFinished(bool success, QString body);

private slots:
  /*! \brief Starts a (re-)fetch of the news feed. */
  void refresh();
  /*! \brief Handles the result of a completed fetch on the GUI thread. */
  void onFetchFinished(bool success, QString body);
  /*! \brief Opens the link of the double-clicked row. */
  void on_news_list_itemDoubleClicked();
  /*! \brief Opens the link of the currently selected row. */
  void on_open_button_clicked();
  /*! \brief Re-fetches the feed. */
  void on_refresh_button_clicked();
};
