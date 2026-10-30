#pragma once
#include "models/Dicom_files.h"

#include <QStandardItemModel>

Q_DECLARE_METATYPE(Dicom_file*)

class File_tree_model : public QStandardItemModel
{
    Q_OBJECT
public:
    /** Sort keys stored on every item; series, studies and patients use their name for all of them. */
    enum Sort_role {
        name_role = Qt::UserRole + 2,
        acquisition_time_role,
        instance_time_role,
        instance_number_role
    };

    File_tree_model(Dicom_files& files);

    void update_model();

private:
    void setup_event_callbacks();
    void add_items();
    void prune_items();
    bool is_file_item_invalid(const QStandardItem&);
    void decorate_file_item(QStandardItem&);
    void set_sort_keys(QStandardItem&, DcmDataset&);

    Dicom_files& m_files;
};
