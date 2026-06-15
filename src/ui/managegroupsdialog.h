/*!
 * \file managegroupsdialog.h
 * \brief Header for the ManageGroupsDialog class.
 */

#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <map>
#include <string>
#include <vector>

namespace Ui
{
class ManageGroupsDialog;
}

/*!
 * \brief Dialog for viewing and editing all version groups belonging to one application.
 *
 * Allows the user to rename a group, edit its notes, switch the active member,
 * and dissolve (remove) the entire group.
 *
 * Rename and notes edits are accumulated locally and only committed — via signals —
 * when the user clicks OK or Apply.  Cancel discards uncommitted edits.
 * Set Active and Dissolve take effect immediately (structural changes).
 *
 * The dialog emits signals instead of calling ModdedApplication directly so that
 * it can be connected to ApplicationManager in the normal Limo signal/slot pattern.
 */
class ManageGroupsDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Initializes the UI.
   * \param parent Parent widget.
   */
  explicit ManageGroupsDialog(QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~ManageGroupsDialog();

  /*!
   * \brief Populates the dialog with all groups for the given application.
   * \param app_id Target application id.
   * \param group_count Total number of groups.
   * \param group_names User-visible name for each group (may be empty).
   * \param group_notes Notes for each group (may be empty).
   * \param group_members Mod ids belonging to each group.
   * \param active_members Active member mod id for each group.
   * \param mod_names Mapping from mod id to mod name (used to display members).
   */
  void setupDialog(int app_id,
                   int group_count,
                   const std::vector<std::string>& group_names,
                   const std::vector<std::string>& group_notes,
                   const std::vector<std::vector<int>>& group_members,
                   const std::vector<int>& active_members,
                   const std::map<int, std::string>& mod_names);

signals:
  /*!
   * \brief Requests renaming a group.
   * \param app_id Target application.
   * \param group Group index.
   * \param name New name.
   */
  void groupRenamed(int app_id, int group, QString name);

  /*!
   * \brief Requests updating the notes of a group.
   * \param app_id Target application.
   * \param group Group index.
   * \param notes New notes.
   */
  void groupNotesChanged(int app_id, int group, QString notes);

  /*!
   * \brief Requests changing the active member of a group.
   * \param app_id Target application.
   * \param group Group index.
   * \param mod_id New active member mod id.
   */
  void activeGroupMemberChanged(int app_id, int group, int mod_id);

  /*!
   * \brief Requests dissolving (removing) a group by removing all its members from it.
   * \param app_id Target application.
   * \param group Group index.
   */
  void groupDissolved(int app_id, int group);

private slots:
  /*! \brief Updates the detail panel when the selected group changes. */
  void on_group_list_currentRowChanged(int row);

  /*! \brief Stages a rename for the current group (no signal emitted yet). */
  void on_rename_button_clicked();

  /*! \brief Stages a notes update for the current group (no signal emitted yet). */
  void on_save_notes_button_clicked();

  /*! \brief Changes the active member to the currently selected member (immediate). */
  void on_set_active_button_clicked();

  /*! \brief Dissolves the currently selected group (immediate, with confirmation). */
  void on_dissolve_button_clicked();

  /*! \brief Commits all pending edits and closes the dialog. */
  void on_buttonBox_accepted();

  /*! \brief Commits all pending edits without closing the dialog. */
  void on_apply_button_clicked();

  /*! \brief Discards all pending edits and closes the dialog. */
  void on_buttonBox_rejected();

private:
  /*! \brief Auto-generated UI. */
  Ui::ManageGroupsDialog* ui;
  /*! \brief Application id passed to setupDialog. */
  int app_id_ = -1;
  /*! \brief Index of the currently displayed group (-1 if none). */
  int current_group_ = -1;
  /*! \brief Group names parallel to the list widget rows. */
  std::vector<std::string> group_names_;
  /*! \brief Group notes parallel to the list widget rows. */
  std::vector<std::string> group_notes_;
  /*! \brief Group member lists parallel to the list widget rows. */
  std::vector<std::vector<int>> group_members_;
  /*! \brief Active member mod ids parallel to the list widget rows. */
  std::vector<int> active_members_;
  /*! \brief Mod-id to mod-name map for display. */
  std::map<int, std::string> mod_names_;

  /*!
   * \brief Pending name edits keyed by group index.
   *
   * Only populated when the user clicks Rename; cleared after commit.
   */
  std::map<int, std::string> pending_names_;

  /*!
   * \brief Pending notes edits keyed by group index.
   *
   * Only populated when the user clicks Save Notes; cleared after commit.
   */
  std::map<int, std::string> pending_notes_;

  /*! \brief Emits signals for all entries in pending_names_ and pending_notes_. */
  void commitPendingEdits();

  /*! \brief Refreshes the detail panel for the current group. */
  void refreshDetailPanel();

  /*! \brief Returns the display string for a group at index \p idx. */
  QString groupDisplayName(int idx) const;
};
