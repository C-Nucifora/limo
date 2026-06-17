/*!
 * \file modpacksdialog.h
 * \brief Header for the ModpacksDialog class.
 */

#pragma once

#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>


class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;

/*!
 * \brief fork #232/#242: Manages first-class modpacks for the active profile.
 *
 * A pack is a named, ordered set of mods (distinct from tags). Any number can be active at once;
 * the deployed set is the union of the active packs' mods, ordered by pack priority then in-pack
 * order. This dialog lets the user create/rename/delete packs, edit their notes and membership
 * (with ordering), and toggle which are active.
 */
class ModpacksDialog : public QDialog
{
  Q_OBJECT

public:
  /*! \brief Builds the dialog UI. */
  explicit ModpacksDialog(QWidget* parent = nullptr);

  /*!
   * \brief Populates the dialog from a JSON payload describing the app's packs and mods.
   *
   * Expected shape: {"active":["name",...], "packs":[{"name","notes","mods":[id,...]}],
   * "mods":[{"id","name"}]}.
   * \param json The pack/mod payload.
   */
  void setPackData(const QString& json);

signals:
  /*! \brief Emitted when the user toggles a pack on or off. */
  void packToggled(QString pack_name, bool active);
  /*! \brief Emitted to create a new, empty pack. */
  void newPackRequested(QString pack_name);
  /*! \brief Emitted to rename a pack. */
  void renamePackRequested(QString old_name, QString new_name);
  /*! \brief Emitted to remove a pack. */
  void removePackRequested(QString pack_name);
  /*! \brief Emitted to set a pack's notes. */
  void setPackNotesRequested(QString pack_name, QString notes);
  /*! \brief Emitted to set a pack's ordered member mod ids. */
  void setPackModsRequested(QString pack_name, QList<int> mod_ids);

private slots:
  void onPackItemChanged(QListWidgetItem* item);
  void onPackSelectionChanged();
  void onNewPackClicked();
  void onRenamePackClicked();
  void onRemovePackClicked();
  void onEditNotesClicked();
  void onAddModsClicked();
  void onRemoveModClicked();
  void onMoveModUpClicked();
  void onMoveModDownClicked();

private:
  /*! \brief Name of the currently selected pack, or empty. */
  QString selectedPack() const;
  /*! \brief Rebuilds the member list for the selected pack. */
  void refreshMembers();
  /*! \brief Emits setPackModsRequested for the selected pack from the member list. */
  void commitMembers();

  QListWidget* pack_list_ = nullptr;
  QListWidget* member_list_ = nullptr;
  QLabel* notes_label_ = nullptr;
  QPushButton* rename_button_ = nullptr;
  QPushButton* remove_button_ = nullptr;
  QPushButton* notes_button_ = nullptr;
  QPushButton* add_mods_button_ = nullptr;
  QPushButton* remove_mod_button_ = nullptr;
  QPushButton* up_button_ = nullptr;
  QPushButton* down_button_ = nullptr;

  /*! \brief True while setPackData repopulates, to suppress itemChanged signals. */
  bool updating_ = false;
  /*! \brief Pack names in priority order. */
  QStringList pack_order_;
  /*! \brief Active pack names. */
  QStringList active_;
  /*! \brief Pack name → notes. */
  QMap<QString, QString> pack_notes_;
  /*! \brief Pack name → ordered member mod ids. */
  QMap<QString, QList<int>> pack_mods_;
  /*! \brief Mod id → display name (all app mods). */
  QMap<int, QString> mod_names_;
};
