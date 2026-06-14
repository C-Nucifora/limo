#include "tagcheckbox.h"


TagCheckBox::TagCheckBox(const QString& tag_name, int num_mods)
{
  tag_name_ = tag_name;
  setText(tag_name + " [" + QString::number(num_mods) + " mod" + (num_mods != 1 ? "s]" : "]"));
  setTristate(true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
  connect(this, &QCheckBox::checkStateChanged, this, &TagCheckBox::onChecked);
#else
  // checkStateChanged(Qt::CheckState) was added in Qt 6.7; fall back to the
  // older stateChanged(int) signal on earlier Qt 6 (e.g. Ubuntu 24.04's 6.4).
  connect(this,
          &QCheckBox::stateChanged,
          this,
          [this](int state) { onChecked(static_cast<Qt::CheckState>(state)); });
#endif
  setStyleSheet(style_sheet);
}

void TagCheckBox::onChecked(Qt::CheckState state)
{
  emit tagBoxChecked(tag_name_, static_cast<int>(state));
}
