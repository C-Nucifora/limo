/*!
 * \file loadorderbisectdialog.cpp
 * \brief Implementation of the LoadOrderBisectDialog class
 */

#include "loadorderbisectdialog.h"
#include "ui_loadorderbisectdialog.h"
#include <QString>
#include <numeric>


LoadOrderBisectDialog::LoadOrderBisectDialog(QWidget* parent) :
  QDialog(parent), ui(new Ui::LoadOrderBisectDialog)
{
  ui->setupUi(this);
  setWindowTitle("Load Order Bisect");
  connect(ui->problem_present_button,
          &QPushButton::clicked,
          this,
          &LoadOrderBisectDialog::onProblemPresentClicked);
  connect(ui->problem_absent_button,
          &QPushButton::clicked,
          this,
          &LoadOrderBisectDialog::onProblemAbsentClicked);
  connect(
    ui->restart_button, &QPushButton::clicked, this, &LoadOrderBisectDialog::onRestartClicked);
}

LoadOrderBisectDialog::~LoadOrderBisectDialog()
{
  delete ui;
}

void LoadOrderBisectDialog::setEntries(const std::vector<Entry>& entries)
{
  entries_ = entries;
  restart();
}

void LoadOrderBisectDialog::restart()
{
  candidates_.resize(entries_.size());
  std::iota(candidates_.begin(), candidates_.end(), 0);
  current_half_.clear();
  nextStep();
}

void LoadOrderBisectDialog::nextStep()
{
  if(candidates_.size() <= 1)
  {
    reportResult();
    return;
  }
  // Test the first half of the remaining candidates.
  const int half = static_cast<int>((candidates_.size() + 1) / 2);
  current_half_.clear();
  for(int i = 0; i < half; i++)
    current_half_.push_back(i);
  updateDisplay();
}

void LoadOrderBisectDialog::updateDisplay()
{
  ui->problem_present_button->setEnabled(true);
  ui->problem_absent_button->setEnabled(true);

  ui->status_label->setText(
    QString("Candidates remaining: %1").arg(candidates_.size()));

  ui->instructions_label->setText(
    "Enable ONLY the mods/plugins listed below (disable all others), then test "
    "your application. Afterwards, report whether the problem still occurred.");

  ui->candidate_list->clear();
  for(int idx : current_half_)
  {
    const int entry_idx = candidates_[idx];
    ui->candidate_list->addItem(
      QString::number(entry_idx + 1) + ": " +
      QString::fromStdString(entries_[entry_idx].name));
  }
}

void LoadOrderBisectDialog::onProblemPresentClicked()
{
  // Problem is in the tested half: keep only those candidates.
  std::vector<int> next;
  next.reserve(current_half_.size());
  for(int idx : current_half_)
    next.push_back(candidates_[idx]);
  candidates_ = std::move(next);
  nextStep();
}

void LoadOrderBisectDialog::onProblemAbsentClicked()
{
  // Problem is in the other half: keep the complement.
  std::vector<int> next;
  std::vector<bool> in_half(candidates_.size(), false);
  for(int idx : current_half_)
    in_half[idx] = true;
  for(int i = 0; i < static_cast<int>(candidates_.size()); i++)
  {
    if(!in_half[i])
      next.push_back(candidates_[i]);
  }
  candidates_ = std::move(next);
  nextStep();
}

void LoadOrderBisectDialog::onRestartClicked()
{
  restart();
}

void LoadOrderBisectDialog::reportResult()
{
  ui->problem_present_button->setEnabled(false);
  ui->problem_absent_button->setEnabled(false);
  ui->candidate_list->clear();

  if(entries_.empty())
  {
    ui->status_label->setText("No load order entries to bisect.");
    ui->instructions_label->setText(
      "There are no mods/plugins in the current load order.");
    return;
  }
  if(candidates_.empty())
  {
    ui->status_label->setText("No suspect found.");
    ui->instructions_label->setText(
      "The problem could not be attributed to any single entry. It may be "
      "caused by an interaction between mods, or it occurs regardless of the "
      "load order. Restart to try again.");
    return;
  }

  const int entry_idx = candidates_.front();
  ui->status_label->setText("Suspected culprit found.");
  ui->instructions_label->setText(
    "The problem is most likely caused by:\n\n" +
    QString::number(entry_idx + 1) + ": " +
    QString::fromStdString(entries_[entry_idx].name) +
    "\n\nTry disabling or updating this mod/plugin. Restart to bisect again.");
}
