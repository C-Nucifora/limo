/*!
 * \file modlistmodel.h
 * \brief Header for the ModListModel class.
 */

#pragma once

#include "../core/modinfo.h"
#include "modlistproxymodel.h"
#include <QAbstractTableModel>
#include <QColor>
#include <QComboBox>
#include <QPixmap>
#include <set>
#include <unordered_map>

class QNetworkAccessManager;
class QNetworkReply;


/*!
 * \brief Manages and provides access to the data displayed in the mod list.
 */
class ModListModel : public QAbstractTableModel
{
  Q_OBJECT

public:
  /*!
   * \brief Constructor.
   * \param proxy The proxy model used to sort and filter data from this model.
   * \param parent The QTableView used to display the data in this mode.
   */
  explicit ModListModel(ModListProxyModel* proxy, QObject* parent = nullptr);

  /*! \brief Index of the action column. */
  static constexpr int action_col = 0;
  /*! \brief Index of the mod name column. */
  static constexpr int name_col = 1;
  /*! \brief Index of the mod version column. */
  static constexpr int version_col = 2;
  /*! \brief Index of the mod id column. */
  static constexpr int id_col = 3;
  /*! \brief Index of the installation time column. */
  static constexpr int time_col = 4;
  /*! \brief Index of the mod size column. */
  static constexpr int size_col = 5;
  /*! \brief Index of the deployers column. */
  static constexpr int deployers_col = 6;
  /*! \brief Index of the tags column. */
  static constexpr int tags_col = 7;
  /*! \brief Index of the status column. */
  static constexpr int status_col = 8;
  /*! \brief fork #198: Index of the category column. Appended last to keep existing indices stable. */
  static constexpr int category_col = 9;

  /*! \brief Role representing the version of a mod. */
  static constexpr int version_list_role = 256;
  /*! \brief Role representing the id of a mod. */
  static constexpr int mod_id_role = 257;
  /*! \brief Role representing the active member of the group a mod belongs to. */
  static constexpr int active_index_role = 258;
  /*! \brief Role representing the members of the group a mod belongs to. */
  static constexpr int group_members_role = 259;
  /*! \brief Role representing the id of the group a mod belongs to. */
  static constexpr int mod_group_role = 260;
  /*! \brief Role representing the name of a mod. */
  static constexpr int mod_name_role = 263;
  /*! \brief Role representing the ids of all deployers managing the given mod. */
  static constexpr int deployer_ids_role = 265;
  /*! \brief Role used for sorting data. */
  static constexpr int sort_role = 266;
  /*! \brief Role representing the icon of the current cell. */
  static constexpr int icon_role = 267;
  /*! \brief Role representing a mods status for every deployer. */
  static constexpr int statuses_role = 268;
  /*! \brief Role representing the set of manual tags added to a mod. */
  static constexpr int manual_tags_role = 269;
  /*! \brief Role representing the set of auto tags added to a mod. */
  static constexpr int auto_tags_role = 270;
  /*! \brief Role representing the path to the local source archive or directory used for
   * installation. */
  static constexpr int local_source_role = 271;
  /*! \brief Role representing the URL from which the mod was downloaded. */
  static constexpr int remote_source_role = 272;
  /*! \brief Role representing whether or not there is an update available for a mod. */
  static constexpr int has_update_role = 273;
  /*! \brief Role representing the mod size on disk. */
  static constexpr int mod_size_role = 274;
  /*! \brief Role representing the version of a mod. */
  static constexpr int mod_version_role = 275;
  static constexpr int expansion_role = 276;
  /*! \brief Role representing the note text for a mod. */
  static constexpr int mod_note_role = 277;
  /*! \brief Role representing whether a mod has a pinned version. */
  static constexpr int mod_pinned_role = 278;
  /*! \brief Role representing the pinned version string of a mod. */
  static constexpr int mod_pinned_version_role = 279;
  /*! \brief fork #198: Role representing the free-text category of a mod. */
  static constexpr int category_role = 280;
  /*! \brief Role representing whether update notifications are ignored for a mod. */
  static constexpr int is_update_ignored_role = 281;

  /*!
   * \brief Represents the overall state of a mod as shown in the status column.
   *
   * Values are ordered so they form a stable, sortable key.
   */
  enum class ModStatus
  {
    /*! \brief Mod has no remote source and is only tracked locally. */
    local = 0,
    /*! \brief Mod has a remote source and matches the latest known remote version. */
    up_to_date = 1,
    /*! \brief Mod has a remote source and a newer version is available remotely. */
    update_available = 2
  };

  /*!
   * \brief Returns the horizontal header section names.
   * \param section Target section.
   * \param orientation Header orientation.
   * \param role Data role.
   * \return Name of the section.
   */
  QVariant headerData(int section,
                      Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;
  /*!
   * \brief Returns the number of rows to display.
   * \param parent Parent index.
   * \return The number of rows.
   */
  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  /*!
   * \brief Returns the number of columns to display.
   * \param parent Parent index.
   * \return The number of columns.
   */
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  /*!
   * \brief Used to access the data stored in this model.
   *
   * Returns data depending on the given role and index. Qt standard roles are
   * used to provide data displayed in views. Custom roles defined in this file
   * provide access to the raw data.
   * \param index Hold row and column for which to return data. Column data is ignored.
   * \param role Describes type of data to return.
   * \return The requested data.
   */
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  /*!
   * \brief Returns the data at the given row and column.
   * \param role Describes type of data to return.
   * \param row Row to access.
   * \param col Column to access.
   * \return The requested data.
   */
  QVariant data(int role, int row, int col = 0) const;
  /*!
   * \brief Returns the flags for the given index. Adds editing flags for editable columns.
   * \param index Target index.
   * \return The flags.
   */
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  /*!
   * \brief Updates all data stored in this model with the given data.
   * \param mods Data for all mods managed by this model.
   */
  void setModInfo(const std::vector<ModInfo>& mods);
  /*!
   * \brief Returns a map with mod ids as keys and the group they belong to as values.
   * \return The map.
   */
  const std::map<int, int>& getGroupMap() const;
  /*!
   * \brief Returns all mods stored in this model.
   * \return A vector of all mods.
   */
  const std::vector<ModInfo>& getModInfo() const;
  /*! \brief Returns true iff this models data is editable in a view. */
  bool isEditable() const;
  /*!
   * \brief Enables or disables the ability to edit this models data in a view.
   * \param is_editable Model data will be editable if this is true.
   */
  void setIsEditable(bool is_editable);
  /*!
   * \brief Sets the currently selected mod and the set of mods which conflict with it.
   *
   * Rows whose mod id is contained in \p conflicting_mod_ids are highlighted in
   * \ref data via \ref Qt::BackgroundRole. The selected mod itself receives a
   * distinct highlight. Highlight colours are derived from the application palette
   * so they remain legible in both light and dark themes.
   *
   * To wire this up to selection changes, mainwindow needs a single connection, e.g.:
   * \code
   * connect(mod_list_view->selectionModel(), &QItemSelectionModel::currentRowChanged,
   *         this, [this](const QModelIndex& cur){ <compute conflicts>;
   *               mod_list_model->setConflictHighlight(selected_id, conflicting_ids); });
   * \endcode
   * \param selected_mod_id Id of the currently selected mod, or -1 for none.
   * \param conflicting_mod_ids Ids of all mods which conflict with the selected mod.
   */
  void setConflictHighlight(int selected_mod_id, const std::set<int>& conflicting_mod_ids);
  /*! \brief Clears the conflict highlight set, causing the view to repaint without highlights. */
  void clearConflictHighlight();
  // fork #199: per-mod highlight colour labels.
  /*!
   * \brief Sets the user-assigned highlight colours for all mods, replacing any previous map.
   *
   * Keys are mod ids, values are hex colour strings (e.g. "#ff0000"). Empty or invalid
   * strings are ignored. Rendered as the row background via \ref Qt::BackgroundRole, layered
   * below the selection and conflict highlights so those always win.
   * \param colors Map of mod id to hex colour string.
   */
  void setModColors(const std::map<int, std::string>& colors);

private:
  /*! \brief The proxy model used to sort and filter this model. */
  ModListProxyModel* proxy_model_;
  /*! \brief All mods which are either not part of a group or the groups active member. */
  std::vector<ModInfo> active_mods_;
  /*! \brief All mods. */
  std::vector<ModInfo> mods_;
  /*! \brief Maps mod ids to the group they belong to. */
  std::map<int, int> group_map_;
  /*! \brief For every group: A vector of mod ids in that group. */
  std::map<int, std::vector<int>> groups_;
  /*! \brief For every group: A QStringList of group version strings. */
  std::map<int, QStringList> group_versions_;
  /*! \brief Maps groups to their active members. */
  std::map<int, int> active_group_members_;
  /*! \brief The mods activation status for every \ref Deployer "deployer" it belongs to. */
  std::map<int, std::vector<bool>> deployer_statuses_;
  /*! \brief Stores whether the model can be edited in a view. */
  bool is_editable_ = true;
  /*! \brief Maps mod ids to a vector of manual tags associated with that mod. */
  std::map<int, std::vector<std::string>> manual_tag_map_;
  /*! \brief Maps mod ids to a vector of auto tags associated with that mod. */
  std::map<int, std::vector<std::string>> auto_tag_map_;
  /*! \brief Maps mod ids to a string representing their size on disk. */
  std::map<int, QString> mod_size_strings_;
  /*! \brief Id of the currently selected mod for conflict highlighting, or -1 for none. */
  int highlight_selected_mod_id_ = -1;
  /*! \brief Ids of all mods which conflict with the currently selected mod. */
  std::set<int> highlight_conflicting_mod_ids_;
  /*! \brief fork #199: maps mod ids to their user-assigned highlight colour. */
  std::map<int, QColor> mod_color_map_;

  /*! \brief Edge length, in pixels, of the preview thumbnails shown in the name column. */
  static constexpr int thumbnail_size = 40;
  /*!
   * \brief In-memory cache mapping mod ids to their (downscaled) preview thumbnail.
   *
   * Mutable because thumbnails are populated lazily from within the const \ref data method.
   */
  mutable std::unordered_map<int, QPixmap> thumbnail_cache_;
  /*! \brief Mod ids whose thumbnail is currently being fetched, to avoid duplicate requests. */
  mutable std::set<int> thumbnail_pending_;
  /*! \brief Mod ids whose thumbnail could not be loaded, to avoid hammering on every repaint. */
  mutable std::set<int> thumbnail_failed_;
  /*! \brief Network manager used to fetch thumbnails off the UI thread. Created on demand. */
  mutable QNetworkAccessManager* network_manager_ = nullptr;
  /*! \brief Directory used to persist thumbnails between runs. */
  QString thumbnail_dir_;

  /*!
   * \brief Checks if the mod at the given mod has an update.
   * \param row Row of the mod.
   * \return True of the mod has an update.
   */
  bool modHasUpdate(int row) const;
  /*!
   * \brief Computes the status of the mod at the given row from its ModInfo fields.
   * \param row Row of the mod.
   * \return The mods status.
   */
  ModStatus modStatus(int row) const;
  /*!
   * \brief Returns the NexusMods preview thumbnail URL for the given mod, if any.
   *
   * The local \ref Mod struct does not currently persist a dedicated picture URL (only the
   * nexus::Mod API response exposes picture_url), so this derives a URL from the mod's
   * remote_source when it points at a NexusMods image. Returns an empty string if unavailable.
   * \param mod The mod to inspect.
   * \return The thumbnail URL, or an empty string.
   */
  QString thumbnailUrl(const Mod& mod) const;
  /*!
   * \brief Returns the on-disk cache path for the given mod's thumbnail.
   * \param mod_id The mod's id.
   * \return Absolute path to the cached thumbnail file.
   */
  QString thumbnailCachePath(int mod_id) const;
  /*!
   * \brief Returns a cached thumbnail for the given mod if available, otherwise kicks off a
   * background fetch and returns an empty pixmap.
   *
   * Never blocks: only memory/disk caches are consulted synchronously; missing thumbnails are
   * fetched asynchronously, after which \ref dataChanged is emitted for the mod's name cell.
   * \param row The mod's row.
   * \return The thumbnail pixmap, or an empty/null QVariant if not (yet) available.
   */
  QVariant thumbnailForRow(int row) const;
  /*!
   * \brief Starts an asynchronous network fetch of the given mod's thumbnail.
   * \param mod_id The mod's id.
   * \param url The thumbnail URL.
   */
  void fetchThumbnail(int mod_id, const QString& url) const;
};
