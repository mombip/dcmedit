#include "models/Dicom_files.h"
#include "models/File_tree_model.h"
#include "test_utils/Temp_dir.h"

#include <catch2/catch.hpp>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <QSortFilterProxyModel>
#include <string>
#include <vector>

TEST_CASE("Files in a series can be sorted by name, acquisition time, instance time and instance number") {
    Temp_dir temp_dir;
    Dicom_files files;
    // name, instance number, acquisition date, acquisition time, instance creation time
    const std::vector<std::vector<const char*>> file_values = {
        {"a.dcm", "10", "20240102", "080000", "100000"},
        {"b.dcm", "1", "20240101", "120000", "090000"},
        {"c.dcm", "2", "20240101", "090000", "110000"},
    };
    for(const auto& values : file_values) {
        DcmFileFormat file;
        DcmDataset* dataset = file.getDataset();
        REQUIRE(dataset->putAndInsertString(DCM_SeriesInstanceUID, "1.2.3").good());
        REQUIRE(dataset->putAndInsertString(DCM_InstanceNumber, values[1]).good());
        REQUIRE(dataset->putAndInsertString(DCM_AcquisitionDate, values[2]).good());
        REQUIRE(dataset->putAndInsertString(DCM_AcquisitionTime, values[3]).good());
        REQUIRE(dataset->putAndInsertString(DCM_InstanceCreationDate, "20240101").good());
        REQUIRE(dataset->putAndInsertString(DCM_InstanceCreationTime, values[4]).good());
        const auto path = temp_dir.path() / values[0];
        REQUIRE(file.saveFile(path.c_str(), EXS_LittleEndianExplicit).good());
        files.open_file(path);
    }
    File_tree_model model(files);
    model.update_model();
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);

    auto sorted_names = [&] (int role) {
        proxy.setSortRole(role);
        proxy.sort(0);
        const QModelIndex series = proxy.index(0, 0, proxy.index(0, 0, proxy.index(0, 0)));
        std::string names;

        for(int row = 0; row < proxy.rowCount(series); ++row) {
            names += proxy.index(row, 0, series).data().toString().right(5).left(1).toStdString();
        }
        return names;
    };
    CHECK(sorted_names(File_tree_model::name_role) == "abc");
    CHECK(sorted_names(File_tree_model::acquisition_time_role) == "cba");
    CHECK(sorted_names(File_tree_model::instance_time_role) == "bac");
    CHECK(sorted_names(File_tree_model::instance_number_role) == "bca");
}
