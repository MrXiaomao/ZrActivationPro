#ifndef QCOMBOBOXDELEGATE_H
#define QCOMBOBOXDELEGATE_H

#include <QItemDelegate>

class QComboBoxDelegate : public QItemDelegate
{
    Q_OBJECT
public:
    explicit QComboBoxDelegate(QObject *parent = nullptr);
    explicit QComboBoxDelegate(QStringList list, QObject *parent = nullptr);
    ~QComboBoxDelegate();

    // 创建编辑器
    virtual QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    // 设置编辑器数据
    virtual void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    // 更新编辑器集合属性
    virtual void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    // 设置模型数据
    virtual void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;


    // 插入
    void insertItem(QString str);
    // 删除
    void removeItem(QString str);

private:
    QStringList mComboBoxItems;
};

#endif // QCOMBOBOXDELEGATE_H
