#include "assetpreviewdialog.h"
#include "ui_assetpreviewdialog.h"
#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImageReader>
#include <QPixmap>
#include <QResizeEvent>
#include <QStringConverter>
#include <QTextStream>
#include <cstdint>
#include <cstring>
#include <optional>
#include <set>


namespace
{
/*! \brief Extensions shown in the read-only text viewer. */
const std::set<QString> kTextExtensions{ "txt", "ini", "cfg",  "json", "toml", "yaml",
                                         "yml", "xml", "log",  "md",   "psc" };
/*! \brief Image extensions Qt handles natively. */
const std::set<QString> kImageExtensions{ "png", "jpg", "jpeg", "bmp", "gif", "webp" };
/*! \brief Texture extensions decoded via QImageReader and/or a built-in decoder. */
const std::set<QString> kTextureExtensions{ "tga", "dds" };
/*! \brief Mesh extensions shown as information only. */
const std::set<QString> kMeshExtensions{ "nif" };

/*! \brief Returns true if the suffix is in any previewable set. */
bool isPreviewable(const QString& suffix)
{
  return kTextExtensions.contains(suffix) || kImageExtensions.contains(suffix) ||
         kTextureExtensions.contains(suffix) || kMeshExtensions.contains(suffix);
}

/*! \brief Reads a little-endian uint32 from a byte buffer at the given offset. */
uint32_t readU32(const QByteArray& data, int offset)
{
  if(offset < 0 || offset + 4 > data.size())
    return 0;
  const auto* p = reinterpret_cast<const unsigned char*>(data.constData() + offset);
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/*! \brief Formats a byte count as a human-readable string. */
QString humanSize(qint64 bytes)
{
  constexpr double kib = 1024.0;
  if(bytes < 1024)
    return QString("%1 B").arg(bytes);
  double value = bytes / kib;
  if(value < 1024.0)
    return QString("%1 KiB").arg(value, 0, 'f', 1);
  value /= kib;
  if(value < 1024.0)
    return QString("%1 MiB").arg(value, 0, 'f', 1);
  value /= kib;
  return QString("%1 GiB").arg(value, 0, 'f', 1);
}

/*! \brief Decompresses one BC1/BC2/BC3 4x4 block into RGBA pixels. */
void decodeBcBlock(const unsigned char* block,
                   int format, // 1 = BC1, 2 = BC2, 3 = BC3
                   uint32_t out[16])
{
  // Colour block is the last 8 bytes for BC2/BC3, the only 8 bytes for BC1.
  const unsigned char* colour = (format == 1) ? block : block + 8;

  const uint16_t c0 = static_cast<uint16_t>(colour[0] | (colour[1] << 8));
  const uint16_t c1 = static_cast<uint16_t>(colour[2] | (colour[3] << 8));

  auto expand565 = [](uint16_t c, int& r, int& g, int& b)
  {
    r = ((c >> 11) & 0x1F);
    g = ((c >> 5) & 0x3F);
    b = (c & 0x1F);
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
  };

  int r[4], g[4], b[4];
  expand565(c0, r[0], g[0], b[0]);
  expand565(c1, r[1], g[1], b[1]);

  const bool opaque_mode = (format != 1) || (c0 > c1);
  if(opaque_mode)
  {
    r[2] = (2 * r[0] + r[1]) / 3;
    g[2] = (2 * g[0] + g[1]) / 3;
    b[2] = (2 * b[0] + b[1]) / 3;
    r[3] = (r[0] + 2 * r[1]) / 3;
    g[3] = (g[0] + 2 * g[1]) / 3;
    b[3] = (b[0] + 2 * b[1]) / 3;
  }
  else
  {
    r[2] = (r[0] + r[1]) / 2;
    g[2] = (g[0] + g[1]) / 2;
    b[2] = (b[0] + b[1]) / 2;
    r[3] = g[3] = b[3] = 0;
  }

  const uint32_t indices =
    static_cast<uint32_t>(colour[4]) | (static_cast<uint32_t>(colour[5]) << 8) |
    (static_cast<uint32_t>(colour[6]) << 16) | (static_cast<uint32_t>(colour[7]) << 24);

  // Alpha handling.
  uint8_t alpha[16];
  if(format == 1)
  {
    for(int i = 0; i < 16; ++i)
    {
      const int idx = (indices >> (2 * i)) & 0x3;
      alpha[i] = (!opaque_mode && idx == 3) ? 0 : 255;
    }
  }
  else if(format == 2) // BC2: 4-bit explicit alpha in first 8 bytes.
  {
    for(int i = 0; i < 16; ++i)
    {
      const int byte = block[i / 2];
      const int nibble = (i & 1) ? (byte >> 4) : (byte & 0x0F);
      alpha[i] = static_cast<uint8_t>(nibble * 17);
    }
  }
  else // BC3: interpolated alpha in first 8 bytes.
  {
    const int a0 = block[0];
    const int a1 = block[1];
    int a[8];
    a[0] = a0;
    a[1] = a1;
    if(a0 > a1)
    {
      for(int i = 1; i <= 6; ++i)
        a[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    }
    else
    {
      for(int i = 1; i <= 4; ++i)
        a[i + 1] = ((5 - i) * a0 + i * a1) / 5;
      a[6] = 0;
      a[7] = 255;
    }
    uint64_t abits = 0;
    for(int i = 0; i < 6; ++i)
      abits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
    for(int i = 0; i < 16; ++i)
    {
      const int sel = (abits >> (3 * i)) & 0x7;
      alpha[i] = static_cast<uint8_t>(a[sel]);
    }
  }

  for(int i = 0; i < 16; ++i)
  {
    const int idx = (indices >> (2 * i)) & 0x3;
    out[i] = (static_cast<uint32_t>(alpha[i]) << 24) |
             (static_cast<uint32_t>(r[idx]) << 16) | (static_cast<uint32_t>(g[idx]) << 8) |
             static_cast<uint32_t>(b[idx]);
  }
}
} // namespace


AssetPreviewDialog::AssetPreviewDialog(const QString& mod_staging_path,
                                       const QString& mod_name,
                                       QWidget* parent) :
  QDialog(parent), ui(new Ui::AssetPreviewDialog),
  staging_path_(QDir(mod_staging_path).absolutePath())
{
  ui->setupUi(this);
  if(mod_name.isEmpty())
    setWindowTitle("Preview Mod Files");
  else
    setWindowTitle(QString("Preview Files - %1").arg(mod_name));

  ui->text_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

  connect(ui->file_list,
          &QListWidget::currentRowChanged,
          this,
          &AssetPreviewDialog::onFileSelectionChanged);
  connect(ui->close_button, &QPushButton::clicked, this, &AssetPreviewDialog::close);

  scanForFiles(staging_path_);

  if(file_paths_.isEmpty())
  {
    ui->file_list->setEnabled(false);
    ui->header_label->setText("No previewable files found.");
    ui->preview_stack->setCurrentIndex(PAGE_TEXT);
    ui->text_view->setPlaceholderText("No previewable files found.");
  }
  else
  {
    ui->info_label->setVisible(false);
    ui->file_list->setCurrentRow(0);
  }
}

AssetPreviewDialog::~AssetPreviewDialog()
{
  delete ui;
}

void AssetPreviewDialog::scanForFiles(const QString& root)
{
  const QDir base(root);
  // Canonical staging path used to keep the scan inside the staging directory.
  const QString canonical_root = QFileInfo(root).canonicalFilePath();
  QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while(it.hasNext() && file_paths_.size() < MAX_FILES)
  {
    const QString path = it.next();
    const QFileInfo info(path);

    const QString relative = base.relativeFilePath(path);
    if(relative.count('/') > MAX_DEPTH)
      continue;
    if(!isPreviewable(info.suffix().toLower()))
      continue;

    // Skip symlinks and any file that resolves outside the staging directory so
    // the preview cannot follow a link out of the mod's staging path.
    if(info.isSymLink())
      continue;
    const QString canonical = info.canonicalFilePath();
    if(canonical_root.isEmpty() || canonical.isEmpty() ||
       !(canonical == canonical_root || canonical.startsWith(canonical_root + '/')))
      continue;

    file_paths_.append(info.absoluteFilePath());
  }

  file_paths_.sort(Qt::CaseInsensitive);
  for(const QString& path : file_paths_)
    ui->file_list->addItem(base.relativeFilePath(path));
}

void AssetPreviewDialog::onFileSelectionChanged(int index)
{
  if(index < 0 || index >= file_paths_.size())
    return;

  current_image_ = QImage();
  const QString path = file_paths_.at(index);
  const QFileInfo info(path);
  const QString suffix = info.suffix().toLower();

  ui->header_label->setText(
    QString("%1  (%2)").arg(QDir(staging_path_).relativeFilePath(path), humanSize(info.size())));

  try
  {
    if(kTextExtensions.contains(suffix))
      previewText(path);
    else if(kImageExtensions.contains(suffix) || kTextureExtensions.contains(suffix))
      previewImage(path, suffix);
    else if(kMeshExtensions.contains(suffix))
      previewMesh(path);
    else
      previewUnsupported(path);
  }
  catch(const std::exception& e)
  {
    previewUnsupported(path, QString("Failed to read file: %1").arg(e.what()));
  }
  catch(...)
  {
    previewUnsupported(path, "Failed to read file.");
  }
}

void AssetPreviewDialog::previewText(const QString& path)
{
  QFileInfo info(path);
  if(info.size() > MAX_TEXT_SIZE)
  {
    previewUnsupported(path, "File is too large to preview as text.");
    return;
  }

  QFile file(path);
  if(!file.open(QIODevice::ReadOnly))
  {
    previewUnsupported(path, "Could not open file.");
    return;
  }
  const QByteArray bytes = file.read(MAX_TEXT_SIZE);
  file.close();

  // Detect the encoding from a BOM if present, otherwise try UTF-8 and fall
  // back to Latin-1 when the bytes are not valid UTF-8, so non-UTF-8 files are
  // not silently mangled.
  std::optional<QStringConverter::Encoding> bom =
    QStringConverter::encodingForData(bytes);
  QStringConverter::Encoding encoding = bom.value_or(QStringConverter::Utf8);
  QStringDecoder decoder(encoding);
  QString text = decoder.decode(bytes);
  QString encoding_name = QStringConverter::nameForEncoding(encoding);
  if(!bom && decoder.hasError())
  {
    QStringDecoder latin1(QStringConverter::Latin1);
    text = latin1.decode(bytes);
    encoding_name = QStringConverter::nameForEncoding(QStringConverter::Latin1);
  }

  ui->header_label->setText(
    QString("%1  [%2]").arg(ui->header_label->text(), encoding_name));
  setText(text);
}

void AssetPreviewDialog::previewImage(const QString& path, const QString& suffix)
{
  QFileInfo info(path);
  if(info.size() > MAX_IMAGE_SIZE)
  {
    previewUnsupported(path, "File is too large to preview as an image.");
    return;
  }

  // Try Qt's own readers first (covers native formats and, where a plugin is
  // available, TGA/DDS too).
  QImageReader reader(path);
  reader.setDecideFormatFromContent(true);
  QImage image = reader.read();

  if(!image.isNull())
  {
    setImage(image);
    return;
  }

  // Built-in fallback for DDS.
  if(suffix == "dds")
  {
    QString info_text;
    image = decodeDds(path, info_text);
    if(!image.isNull())
    {
      setImage(image);
      return;
    }
    previewUnsupported(path, info_text);
    return;
  }

  previewUnsupported(path, "No image plugin available for this format.");
}

void AssetPreviewDialog::previewMesh(const QString& path)
{
  QFileInfo info(path);
  QString version;

  QFile file(path);
  if(file.open(QIODevice::ReadOnly))
  {
    // NIF files begin with a newline-terminated header string such as
    // "Gamebryo File Format, Version 20.2.0.7" or "NetImmerse File Format, ...".
    const QByteArray head = file.read(128);
    file.close();
    const int nl = head.indexOf('\n');
    if(nl > 0)
    {
      const QString header = QString::fromLatin1(head.constData(), nl).trimmed();
      if(header.contains("File Format", Qt::CaseInsensitive))
        version = header;
    }
  }

  QString message = QString("Mesh preview not supported (size: %1).").arg(humanSize(info.size()));
  if(!version.isEmpty())
    message += QString("\n\nNIF header: %1").arg(version);
  setText(message);
}

void AssetPreviewDialog::previewUnsupported(const QString& path, const QString& message)
{
  QFileInfo info(path);
  QString text = QString("No preview available.\n\nSize: %1").arg(humanSize(info.size()));
  if(!message.isEmpty())
    text += QString("\n\n%1").arg(message);
  setText(text);
}

void AssetPreviewDialog::setText(const QString& text)
{
  ui->text_view->setPlainText(text);
  ui->preview_stack->setCurrentIndex(PAGE_TEXT);
}

void AssetPreviewDialog::setImage(const QImage& image)
{
  current_image_ = image;
  ui->preview_stack->setCurrentIndex(PAGE_IMAGE);

  const QSize avail = ui->image_scroll->viewport()->size();
  QImage scaled = image;
  if(avail.width() > 1 && avail.height() > 1 &&
     (image.width() > avail.width() || image.height() > avail.height()))
  {
    scaled = image.scaled(avail, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  ui->image_view->setPixmap(QPixmap::fromImage(scaled));
}

void AssetPreviewDialog::resizeEvent(QResizeEvent* event)
{
  QDialog::resizeEvent(event);
  if(!current_image_.isNull() && ui->preview_stack->currentIndex() == PAGE_IMAGE)
    setImage(current_image_);
}

QImage AssetPreviewDialog::decodeDds(const QString& path, QString& info_out)
{
  QFile file(path);
  if(!file.open(QIODevice::ReadOnly))
  {
    info_out = "Could not open DDS file.";
    return {};
  }

  // Read the magic + 124-byte header (and possibly a DX10 extension header).
  const QByteArray header = file.read(128);
  if(header.size() < 128 || std::memcmp(header.constData(), "DDS ", 4) != 0)
  {
    info_out = "Not a valid DDS file.";
    return {};
  }

  const uint32_t height = readU32(header, 12);
  const uint32_t width = readU32(header, 16);
  const uint32_t mip_count = readU32(header, 28);

  // Pixel format block starts at offset 76; fourCC at 84.
  const uint32_t pf_flags = readU32(header, 80);
  const uint32_t four_cc_raw = readU32(header, 84);
  const uint32_t rgb_bit_count = readU32(header, 88);
  const uint32_t r_mask = readU32(header, 92);
  const uint32_t g_mask = readU32(header, 96);
  const uint32_t b_mask = readU32(header, 100);
  const uint32_t a_mask = readU32(header, 104);

  char four_cc[5] = { 0 };
  std::memcpy(four_cc, header.constData() + 84, 4);
  const QString four_cc_str = QString::fromLatin1(four_cc, 4).trimmed();

  auto headerInfo = [&](const QString& note) -> QString
  {
    return QString("DDS texture\n\nDimensions: %1 x %2\nFormat: %3\nMip levels: %4\n\n%5")
      .arg(width)
      .arg(height)
      .arg(four_cc_str.isEmpty() ? QString("uncompressed") : four_cc_str)
      .arg(mip_count == 0 ? 1 : mip_count)
      .arg(note);
  };

  if(width == 0 || height == 0 || width > 16384 || height > 16384)
  {
    info_out = headerInfo("Invalid or unsupported texture dimensions.");
    return {};
  }

  constexpr uint32_t DDPF_FOURCC = 0x4;
  constexpr uint32_t DDPF_RGB = 0x40;

  // DX10 extended header: fourCC == "DX10".
  if((pf_flags & DDPF_FOURCC) && four_cc_str == "DX10")
  {
    info_out = headerInfo("DX10 / BC7 and other extended formats are not supported.");
    return {};
  }

  const qint64 pixel_count = static_cast<qint64>(width) * static_cast<qint64>(height);

  // Block-compressed formats.
  if(pf_flags & DDPF_FOURCC)
  {
    int format = 0; // 1=BC1, 2=BC2, 3=BC3
    if(four_cc_str == "DXT1")
      format = 1;
    else if(four_cc_str == "DXT3")
      format = 2;
    else if(four_cc_str == "DXT5")
      format = 3;

    if(format == 0)
    {
      info_out = headerInfo("This compressed format is not supported.");
      return {};
    }

    const int block_bytes = (format == 1) ? 8 : 16;
    const int blocks_x = (static_cast<int>(width) + 3) / 4;
    const int blocks_y = (static_cast<int>(height) + 3) / 4;
    const qint64 needed = static_cast<qint64>(blocks_x) * blocks_y * block_bytes;

    const QByteArray data = file.read(needed);
    if(data.size() < needed)
    {
      info_out = headerInfo("File is truncated.");
      return {};
    }

    QImage image(static_cast<int>(width), static_cast<int>(height), QImage::Format_ARGB32);
    if(image.isNull())
    {
      info_out = headerInfo("Could not allocate image.");
      return {};
    }

    const auto* src = reinterpret_cast<const unsigned char*>(data.constData());
    for(int by = 0; by < blocks_y; ++by)
    {
      for(int bx = 0; bx < blocks_x; ++bx)
      {
        uint32_t pixels[16];
        decodeBcBlock(src + (static_cast<qint64>(by) * blocks_x + bx) * block_bytes,
                      format,
                      pixels);
        for(int py = 0; py < 4; ++py)
        {
          const int y = by * 4 + py;
          if(y >= static_cast<int>(height))
            break;
          auto* line = reinterpret_cast<uint32_t*>(image.scanLine(y));
          for(int px = 0; px < 4; ++px)
          {
            const int x = bx * 4 + px;
            if(x >= static_cast<int>(width))
              break;
            line[x] = pixels[py * 4 + px];
          }
        }
      }
    }
    return image;
  }

  // Uncompressed RGB(A) formats.
  if((pf_flags & DDPF_RGB) && (rgb_bit_count == 32 || rgb_bit_count == 24))
  {
    const int bytes_per_pixel = static_cast<int>(rgb_bit_count) / 8;
    const qint64 needed = pixel_count * bytes_per_pixel;
    const QByteArray data = file.read(needed);
    if(data.size() < needed)
    {
      info_out = headerInfo("File is truncated.");
      return {};
    }

    auto shiftOf = [](uint32_t mask) -> int
    {
      if(mask == 0)
        return -1;
      int shift = 0;
      while(((mask >> shift) & 0x1) == 0 && shift < 32)
        ++shift;
      return shift;
    };
    const int rs = shiftOf(r_mask);
    const int gs = shiftOf(g_mask);
    const int bs = shiftOf(b_mask);
    const int as = shiftOf(a_mask);

    QImage image(static_cast<int>(width), static_cast<int>(height), QImage::Format_ARGB32);
    if(image.isNull())
    {
      info_out = headerInfo("Could not allocate image.");
      return {};
    }

    const auto* src = reinterpret_cast<const unsigned char*>(data.constData());
    for(int y = 0; y < static_cast<int>(height); ++y)
    {
      auto* line = reinterpret_cast<uint32_t*>(image.scanLine(y));
      for(int x = 0; x < static_cast<int>(width); ++x)
      {
        const qint64 off = (static_cast<qint64>(y) * width + x) * bytes_per_pixel;
        uint32_t pixel = 0;
        for(int b = 0; b < bytes_per_pixel; ++b)
          pixel |= static_cast<uint32_t>(src[off + b]) << (8 * b);

        const uint8_t r = (rs >= 0) ? static_cast<uint8_t>((pixel & r_mask) >> rs) : 0;
        const uint8_t g = (gs >= 0) ? static_cast<uint8_t>((pixel & g_mask) >> gs) : 0;
        const uint8_t bch = (bs >= 0) ? static_cast<uint8_t>((pixel & b_mask) >> bs) : 0;
        const uint8_t a =
          (as >= 0 && a_mask != 0) ? static_cast<uint8_t>((pixel & a_mask) >> as) : 255;
        line[x] = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
                  (static_cast<uint32_t>(g) << 8) | bch;
      }
    }
    return image;
  }

  info_out = headerInfo("This DDS pixel format is not supported.");
  return {};
}
