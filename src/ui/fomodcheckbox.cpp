#include "fomodcheckbox.h"


FomodCheckBox::FomodCheckBox(const QString& text,
                             const QString& description,
                             const QString& image_path,
                             QLabel* description_label,
                             QLabel* image_label) :
  description_(description), image_path_(image_path), description_label_(description_label),
  image_label_(image_label)
{
  setText(text);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void FomodCheckBox::enterEvent(QEnterEvent* event)
#else
void FomodCheckBox::enterEvent(QEvent* event)
#endif
{
  description_label_->setText(description_);
  QPixmap pixmap(image_path_);
  if(!pixmap.isNull())
  {
    // Fix limo-app/limo#97: scale to the label's actual size rather than a fixed
    // 512 px cap so the preview fills the available info-panel space.
    QSize target = image_label_->size();
    if(!target.isValid() || target.isEmpty())
      target = { 512, 512 };
    image_label_->setPixmap(pixmap.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
  else
    image_label_->setPixmap(pixmap);
  QCheckBox::enterEvent(event);
}
