/*!
 * \file pluginflagsdialog.h
 * \brief Header for the PluginFlagsDialog class.
 */

// fork #202: plugin ESM/ESL flag awareness.

#pragma once

#include "core/plugindeployer.h"
#include <QDialog>
#include <vector>

namespace Ui
{
class PluginFlagsDialog;
}

/*!
 * \brief Read-only dialog listing the managed plugins together with their ESM (master) and
 * ESL (light) flag state, plus running full/light counts against the engine caps.
 */
class PluginFlagsDialog : public QDialog
{
  Q_OBJECT

public:
  /*!
   * \brief Populates the table and counts from the given plugin flag info.
   * \param plugins Per-plugin flag information, as produced by
   * PluginDeployer::getPluginFlagInfo.
   * \param parent Parent widget.
   */
  explicit PluginFlagsDialog(const std::vector<PluginDeployer::PluginFlagInfo>& plugins,
                             QWidget* parent = nullptr);
  /*! \brief Deletes the UI. */
  ~PluginFlagsDialog();

private:
  /*! \brief Contains auto-generated UI elements. */
  Ui::PluginFlagsDialog* ui;
  /*! \brief Maximum number of FULL plugins the engine supports. */
  static constexpr int FULL_CAP = 254;
  /*! \brief Maximum number of LIGHT plugins the engine supports. */
  static constexpr int LIGHT_CAP = 4096;
};
