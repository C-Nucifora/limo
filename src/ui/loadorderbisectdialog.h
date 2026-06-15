/*!
 * \file loadorderbisectdialog.h
 * \brief Header for the LoadOrderBisectDialog class
 */

#pragma once

#include <QDialog>
#include <string>
#include <vector>


namespace Ui
{
class LoadOrderBisectDialog;
}

/*!
 * \brief Self-contained dialog which helps the user binary-search the load order
 * to isolate a single mod/plugin that causes a crash or bug.
 *
 * The dialog operates purely on a passed-in list of load-order entries. It never
 * touches the actual deployment; it only reports which entry it suspects so the
 * user can act on it. The user repeatedly enables one half of the remaining
 * candidate range, tests their game/application externally, then tells the dialog
 * whether the problem still occurs. This halves the search space each step until
 * a single suspect remains.
 */
class LoadOrderBisectDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief A single load-order entry the bisection operates on.
   */
  struct Entry
  {
    /*! \brief Display name of the mod/plugin. */
    std::string name;
    /*! \brief Original activation status of the entry. */
    bool enabled;
  };

  /*!
   * \brief Initializes the UI.
   * \param parent Parent widget.
   */
  explicit LoadOrderBisectDialog(QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~LoadOrderBisectDialog();
  /*!
   * \brief Sets the load-order entries to bisect and resets the search state.
   * \param entries Load-order entries, in load order.
   */
  void setEntries(const std::vector<Entry>& entries);

private slots:
  /*! \brief Reports that the problem still occurred with the current candidate set. */
  void onProblemPresentClicked();
  /*! \brief Reports that the problem did not occur with the current candidate set. */
  void onProblemAbsentClicked();
  /*! \brief Restarts the bisection from the full load order. */
  void onRestartClicked();

private:
  /*! \brief Contains auto-generated UI elements. */
  Ui::LoadOrderBisectDialog* ui;
  /*! \brief All entries being bisected, in load order. */
  std::vector<Entry> entries_;
  /*! \brief Indices into entries_ which are still candidates for the culprit. */
  std::vector<int> candidates_;
  /*!
   * \brief Indices into candidates_ that form the currently tested half.
   * These are the entries the user is asked to enable for the current test.
   */
  std::vector<int> current_half_;

  /*! \brief Resets the candidate set to the full load order and starts a step. */
  void restart();
  /*! \brief Advances the search: selects the next half to test and updates the UI. */
  void nextStep();
  /*! \brief Reports the final suspected culprit and disables further testing. */
  void reportResult();
  /*! \brief Updates the candidate list / instruction labels for the current step. */
  void updateDisplay();
};
