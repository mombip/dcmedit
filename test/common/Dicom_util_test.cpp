#include "common/Dicom_util.h"
#include "common/Exceptions.h"
#include "test_utils/Temp_dir.h"

#include <catch2/catch.hpp>

#include <cstring>
#include <fstream>
#include <dcmtk/dcmdata/dcelem.h>
#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcmetinf.h>
#include <dcmtk/dcmdata/dcsequen.h>
#include <stdexcept>

TEST_CASE("Testing Dicom_util::set_element with create_if_needed=true") {

    DcmDataset dataset;

    SECTION("an empty tag path causes an exception to be thrown") {
        CHECK_THROWS_AS(Dicom_util::set_element("", "value", true, dataset), std::runtime_error);
	}
    SECTION("an invalid tag path causes an exception to be thrown") {
        CHECK_THROWS_AS(Dicom_util::set_element("foo", "value", true, dataset), Tag_path_not_found_error);
	}
    SECTION("setting a value for a non-leaf element causes an exception to be thrown") {
        CHECK_THROWS_AS(Dicom_util::set_element("(8,6)", "value", true, dataset), std::runtime_error);
    }
    SECTION("adding a new element works") {
        Dicom_util::set_element("(10,10)", "Marcus", true, dataset);
        const char* name = nullptr;
        OFCondition status = dataset.findAndGetString(DCM_PatientName, name);
        REQUIRE(status.good());
        CHECK(std::strcmp(name, "Marcus") == 0);
    }
    SECTION("changing an element works") {
        OFCondition status = dataset.putAndInsertString(DCM_PatientName, "John Doe");
        REQUIRE(status.good());
        Dicom_util::set_element("PatientName", "Marcus", true, dataset);
        const char* name = nullptr;
        status = dataset.findAndGetString(DCM_PatientName, name);
        REQUIRE(status.good());
        CHECK(std::strcmp(name, "Marcus") == 0);
    }
    SECTION("adding a new sequence, item and element works") {
        Dicom_util::set_element("(8,6)[0].PatientName", "Marcus", true, dataset);
        DcmItem* item = nullptr;
        OFCondition status = dataset.findAndGetSequenceItem(DCM_LanguageCodeSequence, item, 0);
        REQUIRE(status.good());
        const char* name = nullptr;
        status = item->findAndGetString(DCM_PatientName, name);
        REQUIRE(status.good());
        CHECK(std::strcmp(name, "Marcus") == 0);
    }
}

TEST_CASE("Testing Dicom_util::set_element with create_if_needed=false") {

    DcmDataset dataset;

    SECTION("an empty tag path causes an exception to be thrown") {
        CHECK_THROWS_AS(Dicom_util::set_element("", "value", false, dataset), std::runtime_error);
	}
    SECTION("an invalid tag path causes an exception to be thrown") {
        CHECK_THROWS_AS(Dicom_util::set_element("foo", "value", false, dataset), Tag_path_not_found_error);
	}
    SECTION("editing a non-leaf element causes an exception to be thrown") {
        CHECK_THROWS_AS(Dicom_util::set_element("(8,6)", "", false, dataset), std::runtime_error);
    }
    SECTION("editing a non-existent element causes an exception to be thrown") {
        CHECK_THROWS_AS(Dicom_util::set_element("(8,8)", "value", false, dataset), Tag_path_not_found_error);
    }
    SECTION("editing an element works") {
        OFCondition status = dataset.putAndInsertString(DCM_PatientName, "John Doe");
        REQUIRE(status.good());
        Dicom_util::set_element("PatientName", "Marcus", false, dataset);
        const char* name = nullptr;
        status = dataset.findAndGetString(DCM_PatientName, name);
        REQUIRE(status.good());
        CHECK(std::strcmp(name, "Marcus") == 0);
    }
}

TEST_CASE("Testing Dicom_util::delete_element") {

    DcmDataset dataset;

    SECTION("an empty tag path causes an exception to be thrown") {
        CHECK_THROWS_AS(Dicom_util::delete_element("", dataset), std::runtime_error);
	}
    SECTION("an invalid tag path causes an exception to be thrown") {
        CHECK_THROWS_AS(Dicom_util::delete_element("foo", dataset), Tag_path_not_found_error);
	}
    SECTION("deleting a non-existent element causes an exception to be thrown") {
        CHECK_THROWS_AS(Dicom_util::delete_element("PatientName", dataset), Tag_path_not_found_error);
	}
    SECTION("deleting an element works") {
        OFCondition status = dataset.putAndInsertString(DCM_PatientName, "John Doe");
        REQUIRE(status.good());
        Dicom_util::delete_element("PatientName", dataset);
        CHECK_FALSE(dataset.tagExists(DCM_PatientName));
    }
}

TEST_CASE("Testing Dicom_util::get_index_nr") {

    DcmDataset dataset;

    SECTION("root object has index 0") {
        CHECK(Dicom_util::get_index_nr(dataset) == 0);
	}
    SECTION("element has correct index") {
        OFCondition status = dataset.putAndInsertString({0x10, 0x10}, "John Doe");
        REQUIRE(status.good());
        status = dataset.putAndInsertString({0x10, 0x20}, "117");
        REQUIRE(status.good());

        DcmElement* element = dataset.getElement(0);
        CHECK(Dicom_util::get_index_nr(*element) == 0);

        element = dataset.getElement(1);
        CHECK(Dicom_util::get_index_nr(*element) == 1);
	}
    SECTION("item has correct index") {
        DcmSequenceOfItems sq({0x8, 0x6});
        auto item1 = new DcmItem();
        OFCondition status = sq.append(item1);
        REQUIRE(status.good());
        auto item2 = new DcmItem();
        status = sq.append(item2);
        REQUIRE(status.good());

        CHECK(Dicom_util::get_index_nr(*item1) == 0);
        CHECK(Dicom_util::get_index_nr(*item2) == 1);
	}
}

TEST_CASE("Testing Dicom_util::find_in_file") {

    Temp_dir temp_dir;
    const auto path = temp_dir.path() / "file.dcm";
    const auto xfer = GENERATE(EXS_LittleEndianExplicit, EXS_LittleEndianImplicit, EXS_BigEndianExplicit);
    const auto sq_encoding = GENERATE(EET_ExplicitLength, EET_UndefinedLength);
    const bool big_endian = xfer == EXS_BigEndianExplicit;
    {
        DcmFileFormat file;
        DcmDataset* dataset = file.getDataset();
        REQUIRE(dataset->putAndInsertString(DCM_PatientName, "Doe").good());
        Dicom_util::set_element("(8,6)[0].PatientID", "1234", true, *dataset);
        Dicom_util::set_element("(8,6)[1].PatientID", "5678", true, *dataset);
        REQUIRE(file.saveFile(path.c_str(), xfer, sq_encoding).good());
    }
    DcmFileFormat file;
    REQUIRE(file.loadFile(path.c_str()).good());
    DcmDataset& dataset = *file.getDataset();
    auto read = [&] (DcmObject& object) {
        auto range = Dicom_util::find_in_file(path, object, xfer);
        REQUIRE(range.has_value());
        return Dicom_util::read_file_bytes(path, range->offset, range->length);
    };
    auto tag_bytes = [&] (uint16_t group, uint16_t element) {
        return big_endian
            ? std::vector<uint8_t>{uint8_t(group >> 8), uint8_t(group), uint8_t(element >> 8), uint8_t(element)}
            : std::vector<uint8_t>{uint8_t(group), uint8_t(group >> 8), uint8_t(element), uint8_t(element >> 8)};
    };
    auto ends_with = [] (const std::vector<uint8_t>& bytes, const std::string& suffix) {
        return bytes.size() >= suffix.size() && std::equal(suffix.rbegin(), suffix.rend(), bytes.rbegin());
    };

    SECTION("a top level element is found with its header and value") {
        DcmElement* element = nullptr;
        REQUIRE(dataset.findAndGetElement(DCM_PatientName, element).good());
        std::vector<uint8_t> bytes = read(*element);
        CHECK(bytes.size() == 12);
        CHECK(std::vector<uint8_t>(bytes.begin(), bytes.begin() + 4) == tag_bytes(0x10, 0x10));
        CHECK(ends_with(bytes, "Doe "));
    }
    SECTION("an element in the second item of a sequence is found") {
        DcmItem* item = nullptr;
        REQUIRE(dataset.findAndGetSequenceItem(DCM_LanguageCodeSequence, item, 1).good());
        DcmElement* element = nullptr;
        REQUIRE(item->findAndGetElement(DCM_PatientID, element).good());
        std::vector<uint8_t> bytes = read(*element);
        CHECK(bytes.size() == 12);
        CHECK(std::vector<uint8_t>(bytes.begin(), bytes.begin() + 4) == tag_bytes(0x10, 0x20));
        CHECK(ends_with(bytes, "5678"));
    }
    SECTION("an item is found from its item tag to its end") {
        DcmItem* item = nullptr;
        REQUIRE(dataset.findAndGetSequenceItem(DCM_LanguageCodeSequence, item, 1).good());
        std::vector<uint8_t> bytes = read(*item);
        CHECK(std::vector<uint8_t>(bytes.begin(), bytes.begin() + 4) == tag_bytes(0xFFFE, 0xE000));
        CHECK(bytes.size() == (sq_encoding == EET_UndefinedLength ? 28u : 20u));
    }
    SECTION("an element added after loading is not found") {
        REQUIRE(dataset.putAndInsertString(DCM_PatientSex, "O").good());
        DcmElement* element = nullptr;
        REQUIRE(dataset.findAndGetElement(DCM_PatientSex, element).good());
        CHECK_FALSE(Dicom_util::find_in_file(path, *element, xfer).has_value());
    }
}

TEST_CASE("Testing Dicom_util::find_in_file with group 0002 elements in the dataset") {

    Temp_dir temp_dir;
    const auto path = temp_dir.path() / "file.dcm";
    const std::string preamble = std::string(128, '\0') + "DICM";
    const std::string meta =
        std::string("\x02\x00\x00\x00UL\x04\x00\x26\x00\x00\x00", 12)
        + std::string("\x02\x00\x02\x00UI\x04\x00", 8) + "1.23"
        + std::string("\x02\x00\x10\x00UI\x12\x00", 8) + std::string("1.2.840.10008.1.2\0", 18);
    const std::string dataset_bytes =
        std::string("\x02\x00\x17\x00\x02\x00\x00\x00", 8) + "AE"
        + std::string("\x10\x00\x10\x00\x04\x00\x00\x00", 8) + "Doe ";
    std::ofstream(path, std::ios_base::binary) << preamble << meta << dataset_bytes;

    DcmFileFormat file;
    REQUIRE(file.loadFile(path.c_str()).good());
    DcmDataset& dataset = *file.getDataset();
    REQUIRE(dataset.getOriginalXfer() == EXS_LittleEndianImplicit);
    const uint64_t dataset_start = preamble.size() + meta.size();

    SECTION("a group 0002 element in the dataset is found right after the meta header") {
        DcmElement* element = nullptr;
        REQUIRE(dataset.findAndGetElement({0x0002, 0x0017}, element).good());
        auto range = Dicom_util::find_in_file(path, *element, EXS_LittleEndianImplicit);
        REQUIRE(range.has_value());
        CHECK(range->offset == dataset_start);
        CHECK(range->length == 10);
    }
    SECTION("a file meta information element is found in the meta header") {
        DcmElement* element = nullptr;
        REQUIRE(file.getMetaInfo()->findAndGetElement(DCM_TransferSyntaxUID, element).good());
        auto range = Dicom_util::find_in_file(path, *element, EXS_LittleEndianImplicit);
        REQUIRE(range.has_value());
        CHECK(range->offset == preamble.size() + 24);
        CHECK(range->length == 26);
    }
    SECTION("the file meta information spans from after the preamble to the dataset") {
        auto range = Dicom_util::find_in_file(path, *file.getMetaInfo(), EXS_LittleEndianImplicit);
        REQUIRE(range.has_value());
        CHECK(range->offset == preamble.size());
        CHECK(range->length == meta.size());
    }
    SECTION("the following element is found after it") {
        DcmElement* element = nullptr;
        REQUIRE(dataset.findAndGetElement(DCM_PatientName, element).good());
        auto range = Dicom_util::find_in_file(path, *element, EXS_LittleEndianImplicit);
        REQUIRE(range.has_value());
        CHECK(range->offset == dataset_start + 10);
        CHECK(range->length == 12);
    }
}

TEST_CASE("Testing Dicom_util::to_hex_dump") {

    SECTION("shows offset, bytes and printable characters") {
        const std::string dump = Dicom_util::to_hex_dump({0x10, 0x00, 'P', 'N'}, 0x20);
        CHECK(dump == "00000020  10 00 50 4e " + std::string(12 * 3, ' ') + " ..PN\n");
    }
}
