#ifndef NEUTRONYIELDCALIBRATION_H
#define NEUTRONYIELDCALIBRATION_H

#include <QWidget>
#include <QItemDelegate>
#include <QLineEdit>

namespace Ui {
class NeutronYieldCalibration;
}

#include <QMessageBox>
class NumberDelegate : public QItemDelegate {
    Q_OBJECT
public:
    NumberDelegate(QObject *parent = nullptr) : QItemDelegate(parent) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QLineEdit *editor = new QLineEdit(parent);
        //QRegExp regExp("^[-+]?[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+)?$");//只允许输入整数或浮点数（支持科学计数）
        //QRegExp regExp("^?[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+)?$");//限制只能输入正数
        QRegExp regExp("^(?=.*[1-9])[0-9]{0,7}(\\.[0-9]{1,7})?([eE][+]?[0-9]{1,7})?$");//正数输入限制（有效数≤7位）
        editor->setValidator(new QRegExpValidator(regExp, editor));
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        QString value = index.model()->data(index, Qt::EditRole).toString();
        static_cast<QLineEdit*>(editor)->setText(value);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        QLineEdit *lineEdit = static_cast<QLineEdit*>(editor);
        QString newValue = lineEdit->text();
        if (newValue == "0" || newValue == "0.0" || newValue == "0e0") {
            QMessageBox::warning(nullptr, "错误", "输入必须大于0");
            return;
        }

        if (newValue.left(1) == "+")
            newValue = newValue.remove(0, 1);

        QString oldValue = index.model()->data(index, Qt::EditRole).toString();

        if (newValue != oldValue) {
            QFont font = model->data(index, Qt::FontRole).value<QFont>();
            font.setBold(true);

            QColor clr = model->data(index, Qt::ForegroundRole).value<QColor>();
            model->setData(index, QColor(Qt::red), Qt::ForegroundRole);
            model->setData(index, clr, Qt::UserRole);
        } else {
            QFont font = model->data(index, Qt::FontRole).value<QFont>();
            font.setBold(false);
            model->setData(index, font, Qt::FontRole);

            QColor clr = model->data(index, Qt::UserRole).value<QColor>();
            model->setData(index, clr, Qt::ForegroundRole);
        }

        model->setData(index, newValue, Qt::EditRole);
    }
};


class NeutronYieldCalibration : public QWidget
{
    Q_OBJECT

public:
    explicit NeutronYieldCalibration(QWidget *parent = nullptr);
    ~NeutronYieldCalibration();

    /**
     * 获取中子产额-初始活度数据对
     */
    static QVector<QPair<double, double>> neutronYield();

private slots:
    void on_pushButton_import_clicked();

    void on_pushButton_export_clicked();

    void on_pushButton_save_clicked();

    void on_pushButton_cancel_clicked();

private:
    Ui::NeutronYieldCalibration *ui;
};

#endif // NEUTRONYIELDCALIBRATION_H
