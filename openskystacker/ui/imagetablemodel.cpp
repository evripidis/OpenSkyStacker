#include "imagetablemodel.h"

using namespace openskystacker;

ImageTableModel::ImageTableModel(QObject *parent) : QAbstractTableModel{parent}
{

}

int ImageTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return list.count();
}

int ImageTableModel::rowCount()
{
    return list.count();
}

int ImageTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 8;
}

QVariant ImageTableModel::data(const QModelIndex &index, int role) const
{
    switch (role) {
    case Qt::BackgroundRole:
        return QVariant(QBrush (QColor(Qt::white)));

    case Qt::FontRole:
        if (list[index.row()]->reference) {
            QFont font;
            font.setBold(true);
            return font;
        }
        return {};

    case Qt::CheckStateRole:
        if (index.column() == 0) {
            if (list[index.row()]->checked == true)
                return Qt::Checked;
            return Qt::Unchecked;
        }
        return {};

    case Qt::DisplayRole: case Qt::EditRole: {

        ImageRecord *image = list[index.row()];

        switch (index.column()) {
        case 1: {
            return image->filename;
        }
        case 2:
            switch(image->type) {
            case ImageRecord::LIGHT: default: return tr("Light");
            case ImageRecord::DARK: return tr("Dark");
            case ImageRecord::DARK_FLAT: return tr("Dark Flat");
            case ImageRecord::FLAT: return tr("Flat");
            case ImageRecord::BIAS: return tr("Bias");
            }
            break;
        case 3: {
            float exp = image->shutter;
            if (exp < 0) {
                return "-";
            } else if (exp < 1) {
                int inverted = round(1.0 / exp);
                return "1/" + QString::number(inverted) + " s";
            } else {
                return QString::number(image->shutter) + " s";
            }
        }
        case 4:
            if (image->iso <= 0)
                return "-";
            else
                return image->iso;
        case 5: {
            std::time_t time = image->timestamp;
            if (time == -1) {
                return "-";
            }

            std::tm *timeinfo = std::localtime(&time);
            if (!timeinfo) {
                return "-";
            } else {
                return std::asctime(timeinfo);
            }
        }
        case 6:
            if (image->width <= 0)
                return "-";
            else
                return image->width;
        case 7:
            if (image->height <= 0)
                return "-";
            else
                return image->height;
        default: return {};
        }
    }
    default:
        return {};
    }
}

QVariant ImageTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};

    switch(section) {
    case 0: return {}; // checked
    case 1: return tr("Filename");
    case 2: return tr("Type");
    case 3: return tr("Exposure");
    case 4: return tr("ISO");
    case 5: return tr("Timestamp");
    case 6: return tr("Width");
    case 7: return tr("Height");
    default: return {};
    }
}

void ImageTableModel::Append(ImageRecord *record)
{
    beginInsertRows({}, list.count(), list.count());
    list.append(record);
    endInsertRows();

    emit dataChanged({},{});
}

ImageRecord *ImageTableModel::At(int i)
{
    return list[i];
}

void ImageTableModel::RemoveAt(int i)
{
    beginRemoveRows({}, i, i);
    list.removeAt(i);
    endRemoveRows();

    emit dataChanged({},{});
}

bool ImageTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    Q_UNUSED(role);

    if (index.column() == 0) {
        bool checked = false;
        if (value.toInt() == Qt::Checked)
            checked = true;
        ImageRecord *record = list.at(index.row());
        record->checked = checked;
    }

    emit dataChanged({},{});

    return true;
}

Qt::ItemFlags ImageTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags flags = QAbstractTableModel::flags(index);
    if (index.column() == 0) {
        flags |= Qt::ItemIsUserCheckable;
        flags |= Qt::ItemIsEnabled;
    }

    return flags;
}
