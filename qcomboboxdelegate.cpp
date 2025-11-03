#include "qcomboboxdelegate.h"
#include <QComboBox>

QComboBoxDelegate::QComboBoxDelegate(QObject *parent)
    : QItemDelegate(parent) {
}

QComboBoxDelegate::QComboBoxDelegate(QStringList list, QObject *parent)
    :QItemDelegate(parent) {
    this->mComboBoxItems = list;
}

QComboBoxDelegate::~QComboBoxDelegate() {

}

// 创建编辑器
QWidget *QComboBoxDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    // 创建自己需要的控件进行返回
    QComboBox *editor = new QComboBox(parent);

    return editor;
}

// 设置编辑器数据
void QComboBoxDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const {
    // 将参数editor转换为对应创建的控件，再进行数据初始设置就行
    QString currentText = index.model()->data(index, Qt::EditRole).toString();

    QComboBox *comboBox = static_cast<QComboBox *>(editor);
    comboBox->addItems(mComboBoxItems);
    comboBox->setCurrentText(currentText);
}

// 更新编辑器集合属性
void QComboBoxDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    // 将编辑器设置为矩形属性
    editor->setGeometry(option.rect);
}

// 设置模型数据
void QComboBoxDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    QComboBox *comboBox = static_cast<QComboBox *>(editor);	// 类型转换
    // 模型（单元格）显示的数据
    model->setData(index, comboBox->currentText(), Qt::EditRole);
}

// 插入数据
void QComboBoxDelegate::insertItem(QString str) {
    this->mComboBoxItems.append(str);
}

// 移除数据
void QComboBoxDelegate::removeItem(QString str) {
    for (int i = 0; i < this->mComboBoxItems.size(); i++) {
        if (str == this->mComboBoxItems[i]) {
            this->mComboBoxItems.removeAt(i);
            return;
        }
    }
}
