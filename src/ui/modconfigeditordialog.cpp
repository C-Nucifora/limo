#include "modconfigeditordialog.h"
#include "ui_modconfigeditordialog.h"
#include <QCloseEvent>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QMessageBox>
#include <QSaveFile>
#include <QTextStream>
#include <set>


ModConfigEditorDialog::ModConfigEditorDialog(const QString& mod_staging_path,
                                             const QString& mod_name,
                                             QWidget* parent) :
  QDialog(parent), ui(new Ui::ModConfigEditorDialog),
  staging_path_(QDir(mod_staging_path).absolutePath())
{
  ui->setupUi(this);
  if(mod_name.isEmpty())
    setWindowTitle("Edit Mod Config Files");
  else
    setWindowTitle(QString("Edit Config Files - %1").arg(mod_name));

  ui->editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

  connect(ui->file_list,
          &QListWidget::currentRowChanged,
          this,
          &ModConfigEditorDialog::onFileSelectionChanged);
  connect(ui->save_button, &QPushButton::clicked, this, &ModConfigEditorDialog::onSaveClicked);
  connect(ui->close_button, &QPushButton::clicked, this, &ModConfigEditorDialog::close);
  connect(
    ui->editor, &QPlainTextEdit::textChanged, this, &ModConfigEditorDialog::onTextChanged);

  scanForFiles(staging_path_);

  if(file_paths_.isEmpty())
  {
    ui->file_list->setEnabled(false);
    ui->editor->setEnabled(false);
    ui->save_button->setEnabled(false);
    ui->editor->setPlaceholderText("No editable config files found.");
  }
  else
  {
    ui->info_label->setVisible(false);
    ui->file_list->setCurrentRow(0);
  }
  updateState();
}

ModConfigEditorDialog::~ModConfigEditorDialog()
{
  delete ui;
}

void ModConfigEditorDialog::scanForFiles(const QString& root)
{
  static const std::set<QString> allowed_extensions{
    "cfg", "ini", "json", "toml", "txt", "yaml", "yml", "xml"
  };

  const QDir base(root);
  QDirIterator it(root,
                  QDir::Files | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while(it.hasNext() && file_paths_.size() < MAX_FILES)
  {
    const QString path = it.next();
    const QFileInfo info(path);

    const QString relative = base.relativeFilePath(path);
    if(relative.count('/') > MAX_DEPTH)
      continue;
    if(!allowed_extensions.contains(info.suffix().toLower()))
      continue;
    if(info.size() > MAX_FILE_SIZE)
      continue;

    file_paths_.append(info.absoluteFilePath());
  }

  // Sort by relative path for a stable, readable order.
  file_paths_.sort(Qt::CaseInsensitive);
  for(const QString& path : file_paths_)
    ui->file_list->addItem(base.relativeFilePath(path));
}

void ModConfigEditorDialog::onFileSelectionChanged(int index)
{
  if(index == current_index_)
    return;
  if(index < 0 || index >= file_paths_.size())
    return;

  if(!confirmDiscardChanges())
  {
    // Revert the selection without re-triggering the load.
    QSignalBlocker blocker(ui->file_list);
    ui->file_list->setCurrentRow(current_index_);
    return;
  }

  QFile file(file_paths_.at(index));
  if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    QMessageBox::warning(
      this, "Failed to open file", QString("Could not open:\n%1").arg(file_paths_.at(index)));
    return;
  }
  QTextStream stream(&file);
  stream.setEncoding(QStringConverter::Utf8);
  const QString content = stream.readAll();
  file.close();

  current_index_ = index;
  {
    QSignalBlocker blocker(ui->editor);
    ui->editor->setPlainText(content);
  }
  unsaved_changes_ = false;
  updateState();
}

void ModConfigEditorDialog::onTextChanged()
{
  if(current_index_ < 0)
    return;
  unsaved_changes_ = true;
  updateState();
}

void ModConfigEditorDialog::onSaveClicked()
{
  if(current_index_ < 0 || current_index_ >= file_paths_.size())
    return;

  QSaveFile file(file_paths_.at(current_index_));
  if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    QMessageBox::warning(this,
                         "Failed to save file",
                         QString("Could not write to:\n%1").arg(file_paths_.at(current_index_)));
    return;
  }
  QTextStream stream(&file);
  stream.setEncoding(QStringConverter::Utf8);
  stream << ui->editor->toPlainText();
  stream.flush();
  if(!file.commit())
  {
    QMessageBox::warning(this,
                         "Failed to save file",
                         QString("Could not write to:\n%1").arg(file_paths_.at(current_index_)));
    return;
  }

  unsaved_changes_ = false;
  updateState();
  emit configSaved();
}

void ModConfigEditorDialog::updateState()
{
  ui->save_button->setEnabled(current_index_ >= 0 && unsaved_changes_);

  QString title = windowTitle();
  const bool has_marker = title.startsWith('*');
  if(unsaved_changes_ && !has_marker)
    setWindowTitle('*' + title);
  else if(!unsaved_changes_ && has_marker)
    setWindowTitle(title.mid(1));
}

bool ModConfigEditorDialog::confirmDiscardChanges()
{
  if(!unsaved_changes_)
    return true;

  const auto answer =
    QMessageBox::question(this,
                          "Unsaved changes",
                          "The current file has unsaved changes. Discard them?",
                          QMessageBox::Discard | QMessageBox::Cancel,
                          QMessageBox::Cancel);
  return answer == QMessageBox::Discard;
}

void ModConfigEditorDialog::closeEvent(QCloseEvent* event)
{
  if(confirmDiscardChanges())
    event->accept();
  else
    event->ignore();
}
