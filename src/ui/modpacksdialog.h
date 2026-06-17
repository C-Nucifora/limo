/*!
 * \file modpacksdialog.h
 * \brief Header for the ModpacksDialog class.
 */

#pragma once

#include <QDialog>
#include <QStringList>


class QListWidget;
class QListWidgetItem;
class QLabel;

/*!
 * \brief fork #232: Lets the user toggle "modpacks" on and off for the active profile.
 *
 * A pack is a manual tag; any number can be active at once and the deployed (enabled) set is
 * the union of all active packs' mods. Toggling a checkbox here activates/deactivates a pack;
 * mod membership is managed through the normal tag UI (a mod can be in several packs).
 */
class ModpacksDialog : public QDialog
{
  Q_OBJECT

public:
  /*! \brief Builds the dialog UI. */
  explicit ModpacksDialog(QWidget* parent = nullptr);

  /*!
   * \brief Populates the pack list, checking the packs that are currently active.
   * \param all_packs    Names of every available pack (manual tag).
   * \param active_packs Names of the packs active in the current profile.
   */
  void setPacks(const QStringList& all_packs, const QStringList& active_packs);

signals:
  /*! \brief Emitted when the user toggles a pack on or off. */
  void packToggled(QString pack_name, bool active);
  /*! \brief Emitted when the user asks to create a new (empty) pack. */
  void newPackRequested(QString pack_name);

private slots:
  /*! \brief Forwards a checkbox change as a packToggled signal (suppressed while populating). */
  void onItemChanged(QListWidgetItem* item);
  /*! \brief Prompts for a name and requests creation of a new pack. */
  void onNewPackClicked();

private:
  /*! \brief Checkable list of packs. */
  QListWidget* list_ = nullptr;
  /*! \brief Shown when there are no packs yet. */
  QLabel* empty_label_ = nullptr;
  /*! \brief True while setPacks() repopulates, to suppress spurious itemChanged signals. */
  bool updating_ = false;
};
