#include "ui/file_tree_view/File_tree_view.h"

#include "models/File_tree_model.h"

#include <QComboBox>
#include <QTreeView>
#include <QVBoxLayout>

File_tree_view::File_tree_view()
    : m_proxy_model(new QSortFilterProxyModel(this)),
      m_tree_view(new QTreeView()) {
    m_proxy_model->setSortRole(File_tree_model::name_role);
    m_tree_view->setModel(m_proxy_model);
    m_tree_view->setHeaderHidden(true);
    m_tree_view->setTextElideMode(Qt::ElideMiddle);
    m_tree_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree_view->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_tree_view, &QTreeView::activated, [this] (auto& index) {
        item_activated(m_proxy_model->mapToSource(index));
    });
    // Load the file as soon as it becomes current, by mouse or keyboard.
    connect(m_tree_view->selectionModel(), &QItemSelectionModel::currentChanged, [this] (auto& index) {
        item_activated(m_proxy_model->mapToSource(index));
    });

    auto sort_box = new QComboBox();
    sort_box->addItem("Sort by name", File_tree_model::name_role);
    sort_box->addItem("Sort by acquisition date/time", File_tree_model::acquisition_time_role);
    sort_box->addItem("Sort by instance creation date/time", File_tree_model::instance_time_role);
    sort_box->addItem("Sort by instance number", File_tree_model::instance_number_role);
    connect(sort_box, qOverload<int>(&QComboBox::currentIndexChanged), [this, sort_box] {
        m_proxy_model->setSortRole(sort_box->currentData().toInt());
        m_proxy_model->sort(0);
    });

    auto widget = new QWidget();
    auto layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(sort_box);
    layout->addWidget(m_tree_view);

    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setWindowTitle("File tree");
    setWidget(widget);
}

void File_tree_view::set_model(File_tree_model& model) {
    m_proxy_model->setSourceModel(&model);
    m_proxy_model->sort(0);
}
