#include "fomoddialog.h"
#include "colors.h"
#include "core/log.h"
#include "fomodcheckbox.h"
#include "fomodradiobutton.h"
#include "ui_fomoddialog.h"
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <format>
#include <fstream>
#include <json/json.h>

namespace sfs = std::filesystem;


FomodDialog::FomodDialog(QWidget* parent) : QDialog(parent), ui(new Ui::FomodDialog)
{
  ui->setupUi(this);
  installer_ = std::make_unique<fomod::FomodInstaller>();
  next_button_ = new QPushButton(this);
  back_button_ = new QPushButton(this);
  back_button_->setText("Back");
  ui->buttonBox->addButton(back_button_, QDialogButtonBox::ApplyRole);
  ui->buttonBox->addButton(next_button_, QDialogButtonBox::ApplyRole);
  connect(next_button_, &QPushButton::pressed, this, &FomodDialog::onNextButtonPressed);
  connect(back_button_, &QPushButton::pressed, this, &FomodDialog::onBackButtonPressed);
}

FomodDialog::~FomodDialog()
{
  delete ui;
}

void FomodDialog::setupDialog(const sfs::path& config_file,
                              const sfs::path& target_path,
                              const QString& app_version,
                              const ImportModInfo& info,
                              int app_id,
                              bool paths_are_case_invariant,
                              const sfs::path& choices_path)
{
  dialog_completed_ = false;
  import_mod_info_ = info;
  app_id_ = app_id;
  paths_are_case_invariant_ = paths_are_case_invariant;
  /* Set up choices persistence (fork #135 / limo-app/limo#256). */
  choices_file_ = choices_path.empty() ? sfs::path{} : choices_path / ".fomod_choices.json";
  saved_choices_.clear();
  current_choices_.clear();
  loadChoices();
  installer_->init(config_file, paths_are_case_invariant, target_path, app_version.toStdString());
  back_button_->setVisible(false);
  has_no_steps_ = installer_->hasNoSteps();
  updateInstallStep();
  updateNextButton();
}

std::vector<std::pair<sfs::path, sfs::path>> FomodDialog::getResult() const
{
  return result_;
}

bool FomodDialog::hasSteps() const
{
  return !has_no_steps_;
}

QAbstractButton* FomodDialog::makeButton(fomod::PluginGroup::Type type,
                                         const QString& text,
                                         const QString& description,
                                         const QString& image_path) const
{
  if(type == fomod::PluginGroup::exactly_one || type == fomod::PluginGroup::at_most_one)
    return new FomodRadioButton(
      text, description, image_path, ui->description_label, ui->image_label);
  return new FomodCheckBox(text, description, image_path, ui->description_label, ui->image_label);
}

void FomodDialog::updateInstallStep(
  std::optional<std::pair<std::vector<std::vector<bool>>, fomod::InstallStep>> prev_step)
{
  if(has_no_steps_)
  {
    result_ = installer_->getInstallationFiles({});
    return;
  }
  std::optional<fomod::InstallStep> step;
  if(!prev_step)
  {
    step = installer_->step(getSelection());
    if(!step)
      return;
  }
  delete ui->group_area->takeWidget();
  qDeleteAll(button_groups_.begin(), button_groups_.end());
  button_groups_.clear();
  group_types_.clear();
  none_groups_.clear();
  if(!prev_step)
    cur_step_ = *step;
  else
    cur_step_ = prev_step->second;
  auto frame = new QFrame();
  ui->group_area->setWidget(frame);
  auto group_layout = new QVBoxLayout();
  frame->setLayout(group_layout);
  auto name_label = new QLabel();
  name_label->setText(QString("## ") + cur_step_.name.c_str());
  name_label->setTextFormat(Qt::MarkdownText);
  group_layout->addWidget(name_label);
  int group_idx = 1;
  for(const auto& group : cur_step_.groups)
  {
    auto button_group = new QButtonGroup;
    auto box = new QGroupBox();
    QString group_title;
    switch(group.type)
    {
      case fomod::PluginGroup::all:
        group_title = " [Select All]";
        break;
      case fomod::PluginGroup::any:
        group_title = " [Select Any]";
        break;
      case fomod::PluginGroup::at_least_one:
        group_title = " [Select At Least One]";
        break;
      default:
        group_title = " [Select One]";
    }
    box->setTitle(group.name.c_str() + group_title);
    auto box_layout = new QVBoxLayout();
    box->setLayout(box_layout);
    if(group.type == group.at_most_one)
    {
      auto button = new FomodRadioButton("None", "", "", ui->description_label, ui->image_label);
      box_layout->addWidget(button);
      button_group->addButton(button, -1);
      none_groups_.insert(group_idx - 1);
    }
    auto unavailable_palette = QApplication::palette();
    auto required_palette = QApplication::palette();
    unavailable_palette.setColor(QPalette::WindowText, colors::RED);
    required_palette.setColor(QPalette::WindowText, colors::ORANGE);
    int plugin_idx = 0;
    for(const auto& plugin : group.plugins)
    {
      auto button = makeButton(
        group.type, plugin.name.c_str(), plugin.description.c_str(), plugin.image_path.c_str());
      connect(button, &QPushButton::clicked, this, &FomodDialog::onPluginSelected);
      QString plugin_type;
      if(plugin.type == fomod::PluginType::recommended)
      {
        plugin_type = "[Recommended] ";
        button->setChecked(true);
      }
      else if(plugin.type == fomod::PluginType::required)
      {
        plugin_type = "[Required] ";
        button->setChecked(true);
        button->setPalette(required_palette);
      }
      else if(plugin.type == fomod::PluginType::not_usable)
      {
        plugin_type = "[Not Available] ";
        button->setPalette(unavailable_palette);
      }
      else
        plugin_type = "";
      if(!plugin.potential_types.empty())
      {
        QString tooltip_text = "Requirements:";
        for(const auto& [type, dependency] : plugin.potential_types)
        {
          tooltip_text +=
            ("\n" + fomod::PLUGIN_TYPE_NAMES[type] + ": " + dependency.toString()).c_str();
        }
        button->setToolTip(tooltip_text);
      }
      button->setText(plugin_type + button->text());
      if(prev_step && !(prev_step->first.empty()))
        button->setChecked(prev_step->first[group_idx - 1][plugin_idx]);
      box_layout->addWidget(button);
      button_group->addButton(button, plugin_idx);
      if(!(group.type == fomod::PluginGroup::at_most_one ||
           group.type == fomod::PluginGroup::exactly_one))
        button_group->setExclusive(false);
      plugin_idx++;
    }
    button_groups_.append(button_group);
    group_types_.append(group.type);
    group_layout->addWidget(box);
    group_idx++;
  }

  /* Pre-select previously saved choices for this step (fork #135 / limo-app/limo#256).
   * Only applied for forward navigation (not stepBack, which restores from prev_selections_). */
  if(!prev_step && !saved_choices_.empty())
  {
    const auto pre_sel = installer_->applyNamedChoices(saved_choices_);
    /* pre_sel is indexed by group, skipping the "None" dummy button in at_most_one groups. */
    for(int gi = 0; gi < button_groups_.size() && gi < static_cast<int>(pre_sel.size()); gi++)
    {
      const auto& group_sel = pre_sel[gi];
      bool any_saved = false;
      for(bool b : group_sel)
        any_saved = any_saved || b;
      if(!any_saved)
        continue; /* No saved choice for this group — keep defaults. */
      const auto buttons = button_groups_[gi]->buttons();
      /* For at_most_one groups, button index 0 is the "None" dummy — skip it. */
      int btn_offset = none_groups_.contains(gi) ? 1 : 0;
      for(int pi = 0; pi < static_cast<int>(group_sel.size()); pi++)
      {
        int btn_idx = pi + btn_offset;
        if(btn_idx < buttons.size())
          buttons[btn_idx]->setChecked(group_sel[pi]);
      }
    }
  }

  group_layout->addStretch();
  next_button_->setEnabled(selectionIsValid());
  updateNextButton();
}

bool FomodDialog::selectionIsValid()
{
  for(int i = 0; i < button_groups_.size(); i++)
  {
    auto type = group_types_[i];
    if(type == fomod::PluginGroup::any)
      continue;
    int num_selected = 0;
    for(auto button : static_cast<const QList<QAbstractButton*>>(button_groups_[i]->buttons()))
    {
      if(button->isChecked())
        num_selected++;
    }
    if(type == fomod::PluginGroup::at_least_one && num_selected == 0 ||
       type == fomod::PluginGroup::at_most_one && num_selected > 1 ||
       type == fomod::PluginGroup::all && num_selected != button_groups_[i]->buttons().size() ||
       type == fomod::PluginGroup::exactly_one && num_selected != 1)
      return false;
  }
  return true;
}

std::vector<std::vector<bool>> FomodDialog::getSelection()
{
  std::vector<std::vector<bool>> selection;
  for(int group_idx = 0; auto group : static_cast<const QList<QButtonGroup*>>(button_groups_))
  {
    std::vector<bool> group_vec;
    for(int button_idx = 0;
        auto button : static_cast<const QList<QAbstractButton*>>(group->buttons()))
    {
      if(!(button_idx == 0 && none_groups_.contains(group_idx)))
        group_vec.push_back(button->isChecked());
      button_idx++;
    }
    selection.push_back(group_vec);
    group_idx++;
  }
  return selection;
}

void FomodDialog::updateNextButton()
{
  next_button_->setEnabled(selectionIsValid());
  auto s = getSelection();
  if(installer_->hasNextStep(getSelection()))
    next_button_->setText("Next");
  else
    next_button_->setText("Finish");
}

void FomodDialog::closeEvent(QCloseEvent* event)
{
  if(dialog_completed_)
    return;
  dialog_completed_ = true;
  emit addModAborted();
  QDialog::reject();
}

void FomodDialog::reject()
{
  if(dialog_completed_)
    return;
  dialog_completed_ = true;
  emit addModAborted();
  QDialog::reject();
}

void FomodDialog::onNextButtonPressed()
{
  /*
   * For an unknown reason this dialog can be accepted multiple times when
   * clicking fast enough. This guards against that.
   */
  if(dialog_completed_)
    return;

  ui->description_label->setText("");
  ui->image_label->setPixmap({});
  if(next_button_->text() == "Next")
  {
    /* Record named choices for the current step before advancing
     * (fork #135 / limo-app/limo#256). */
    const auto step_choices = installer_->getStepChoiceNames(getSelection());
    current_choices_.insert(step_choices.begin(), step_choices.end());
    updateInstallStep();
  }
  else
  {
    /* Record named choices for the final step, then persist to disk
     * (fork #135 / limo-app/limo#256). */
    const auto step_choices = installer_->getStepChoiceNames(getSelection());
    current_choices_.insert(step_choices.begin(), step_choices.end());
    saveChoices();

    dialog_completed_ = true;
    result_ = installer_->getInstallationFiles(getSelection());
    if(result_.empty())
    {
      Log::error("No files to install!");
      emit addModAborted();
    }
    else
    {
      import_mod_info_.files = result_;
      emit addModAccepted(app_id_, import_mod_info_);
    }
    accept();
  }
  if(installer_->hasPreviousStep())
    back_button_->setVisible(true);
}

void FomodDialog::onPluginSelected(bool checked)
{
  updateNextButton();
}

void FomodDialog::onBackButtonPressed()
{
  updateInstallStep(installer_->stepBack());
  if(!installer_->hasPreviousStep())
    back_button_->setVisible(false);
}

void FomodDialog::on_buttonBox_rejected()
{
  if(dialog_completed_)
    return;
  dialog_completed_ = true;
  emit addModAborted();
}

void FomodDialog::loadChoices()
{
  /* Load previously persisted FOMOD choices from the sidecar JSON.
   * Fork #135 / limo-app/limo#256. */
  if(choices_file_.empty() || !sfs::exists(choices_file_))
    return;
  try
  {
    std::ifstream file(choices_file_);
    if(!file.is_open())
      return;
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    if(!Json::parseFromStream(builder, file, &root, &errs))
    {
      Log::warning(std::format("Failed to parse FOMOD choices file '{}': {}",
                               choices_file_.string(),
                               errs));
      return;
    }
    for(const auto& key : root.getMemberNames())
    {
      std::set<std::string> plugin_names;
      for(const auto& name : root[key])
        plugin_names.insert(name.asString());
      saved_choices_[key] = std::move(plugin_names);
    }
  }
  catch(const std::exception& e)
  {
    Log::warning(std::format("Could not load FOMOD choices from '{}': {}",
                             choices_file_.string(),
                             e.what()));
  }
}

void FomodDialog::saveChoices()
{
  /* Persist the current session's named choices to the sidecar JSON.
   * Fork #135 / limo-app/limo#256. */
  if(choices_file_.empty() || current_choices_.empty())
    return;
  try
  {
    Json::Value root(Json::objectValue);
    for(const auto& [key, names] : current_choices_)
    {
      Json::Value arr(Json::arrayValue);
      for(const auto& name : names)
        arr.append(name);
      root[key] = arr;
    }
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::ofstream file(choices_file_);
    if(!file.is_open())
    {
      Log::warning(std::format("Could not open FOMOD choices file '{}' for writing",
                               choices_file_.string()));
      return;
    }
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);
  }
  catch(const std::exception& e)
  {
    Log::warning(std::format("Could not save FOMOD choices to '{}': {}",
                             choices_file_.string(),
                             e.what()));
  }
}

