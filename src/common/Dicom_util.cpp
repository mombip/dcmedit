#include "Dicom_util.h"

#include "common/Exceptions.h"
#include "logging/Log.h"

#include <dcmtk/dcmdata/dcelem.h>
#include <dcmtk/dcmdata/dcitem.h>
#include <dcmtk/dcmdata/dcpath.h>
#include <dcmtk/dcmdata/dcsequen.h>
#include <dcmtk/dcmdata/dcxfer.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

static DcmObject* get_object(DcmPath* path) {
    DcmPathNode* last_node = path->back();
    DcmObject* object = last_node ? last_node->m_obj : nullptr;

    if(object == nullptr) {
        Log::error("Invalid tag path.");
    }
    return object;
}

static void set_element_value(const OFList<DcmPath*>& paths, const std::string& value) {
    for(DcmPath* path : paths) {
        auto element = dynamic_cast<DcmElement*>(get_object(path));

        if(element == nullptr) {
            throw std::runtime_error("failed to get element");
        }
        OFCondition status = element->putString(value.c_str());

        if(status.bad()) {
            throw std::runtime_error(status.text());
        }
    }
}

void Dicom_util::set_element(const std::string& tag_path, const std::string& value,
    bool create_if_needed, DcmObject& object) {
    DcmPathProcessor path_proc;
    OFCondition status = path_proc.findOrCreatePath(&object, tag_path.c_str(), create_if_needed);

    if(status.bad()) {
        throw Tag_path_not_found_error(status.text());
    }
    OFList<DcmPath*> found_paths;
    path_proc.getResults(found_paths);
    DcmObject* path_object = get_object(found_paths.front());

    if(path_object == nullptr) {
        throw std::runtime_error("failed to get object");
    }
    else if(!path_object->isLeaf() && (!create_if_needed || !value.empty())) {
        throw std::runtime_error("can't set value for non-leaf element");
    }
    else if(path_object->isLeaf()) {
        set_element_value(found_paths, value);
    }
}

void Dicom_util::delete_element(const std::string& tag_path, DcmObject& object) {
    DcmPathProcessor path_proc;
    unsigned int result_count = 0;
    OFCondition status = path_proc.findOrDeletePath(&object, tag_path.c_str(), result_count);

    if(status.bad()) {
        throw Tag_path_not_found_error(status.text());
    }
}

int Dicom_util::get_index_nr(DcmObject& object) {
    DcmObject* parent = object.getParent();

    if(!parent) {
        return 0;
    }
    const DcmEVR vr = parent->ident();

    if(vr == EVR_item || vr == EVR_dataset) {
        auto item = static_cast<DcmItem*>(parent);
        auto element_count = item->getNumberOfValues();

        for(auto i = 0u; i < element_count; ++i) {
            if(item->getElement(i) == &object) {
                return i;
            }
        }
    }
    else if(vr == EVR_SQ) {
        auto sq = static_cast<DcmSequenceOfItems*>(parent);
        auto item_count = sq->getNumberOfValues();

        for(auto i = 0u; i < item_count; ++i) {
            if(sq->getItem(i) == &object) {
                return i;
            }
        }
    }
    Log::error("Could not get index of object. Parent VR: " + std::to_string(vr));
    return -1;
}

namespace {
    const uint32_t undefined_length = 0xFFFFFFFF;
    const uint32_t item_tag = 0xFFFEE000;
    const uint32_t item_delimiter_tag = 0xFFFEE00D;
    const uint32_t sequence_delimiter_tag = 0xFFFEE0DD;
    const uint64_t end_of_file = UINT64_MAX;

    struct Encoding
    {
        bool explicit_vr;
        bool big_endian;
    };

    struct Header
    {
        uint32_t tag = 0;
        std::string vr;
        uint32_t length = 0;
        uint64_t start = 0;
        uint64_t value_start = 0;
    };

    // A dataset region ends at its end offset or at an item delimiter, whichever comes first.
    struct Region
    {
        uint64_t start;
        uint64_t end;
    };
}

static uint32_t read_uint(std::istream& in, int size, bool big_endian) {
    unsigned char bytes[4] = {};
    in.read(reinterpret_cast<char*>(bytes), size);
    uint32_t value = 0;

    for(int i = 0; i < size; ++i) {
        value |= static_cast<uint32_t>(bytes[i]) << (8 * (big_endian ? size - 1 - i : i));
    }
    return value;
}

static bool has_long_length(const std::string& vr) {
    static const std::array<const char*, 13> long_vrs =
        {"OB", "OD", "OF", "OL", "OV", "OW", "SQ", "SV", "UC", "UN", "UR", "UT", "UV"};
    return std::find(long_vrs.begin(), long_vrs.end(), vr) != long_vrs.end();
}

static std::optional<Header> read_header(std::istream& in, Encoding encoding) {
    Header header;
    header.start = static_cast<uint64_t>(in.tellg());
    const uint32_t group = read_uint(in, 2, encoding.big_endian);
    header.tag = group << 16 | read_uint(in, 2, encoding.big_endian);

    if(group == 0xFFFE || !encoding.explicit_vr) {
        header.length = read_uint(in, 4, encoding.big_endian);
    }
    else {
        char vr[2] = {};
        in.read(vr, 2);
        header.vr.assign(vr, 2);

        if(has_long_length(header.vr)) {
            in.ignore(2);
            header.length = read_uint(in, 4, encoding.big_endian);
        }
        else {
            header.length = read_uint(in, 2, encoding.big_endian);
        }
    }
    header.value_start = static_cast<uint64_t>(in.tellg());
    return in ? std::optional<Header>(header) : std::nullopt;
}

// UN with undefined length contains implicit VR little endian data (PS3.5 6.2.2).
static Encoding nested_encoding(Encoding encoding, const Header& header) {
    return header.vr == "UN" && header.length == undefined_length ? Encoding{false, false} : encoding;
}

static void skip_value(std::istream& in, Encoding encoding, const Header& header) {
    if(header.length != undefined_length) {
        in.seekg(static_cast<std::streamoff>(header.value_start + header.length));
        return;
    }
    const uint32_t end_tag = header.tag == item_tag ? item_delimiter_tag : sequence_delimiter_tag;
    const Encoding child_encoding = nested_encoding(encoding, header);

    while(true) {
        std::optional<Header> child = read_header(in, child_encoding);

        if(!child) {
            throw std::runtime_error("unexpected end of file");
        }
        if(child->tag == end_tag) {
            return;
        }
        skip_value(in, child_encoding, *child);
    }
}

static Region value_region(const Header& header) {
    const uint64_t end = header.length == undefined_length ? end_of_file : header.value_start + header.length;
    return {header.value_start, end};
}

// Finds an element (tag) or the n-th item (item_tag, n) directly inside a region.
static std::optional<Header> find_child(std::istream& in, Encoding encoding, Region region,
    uint32_t tag, int index) {
    in.clear();
    in.seekg(static_cast<std::streamoff>(region.start));

    while(static_cast<uint64_t>(in.tellg()) < region.end) {
        std::optional<Header> header = read_header(in, encoding);

        if(!header || header->tag == item_delimiter_tag || header->tag == sequence_delimiter_tag) {
            break;
        }
        if(header->tag == tag && index-- == 0) {
            return header;
        }
        skip_value(in, encoding, *header);
    }
    return std::nullopt;
}

// Returns the region of the file meta information; the dataset starts at its end.
static Region find_meta_region(std::istream& in) {
    char magic[4] = {};
    in.seekg(128);
    in.read(magic, 4);
    const uint64_t start = in && std::equal(magic, magic + 4, "DICM") ? 132 : 0;
    in.clear();
    in.seekg(static_cast<std::streamoff>(start));

    // The meta header is always explicit VR little endian. Like DCMTK, trust its group length if present,
    // since a dataset may itself contain group 0002 elements.
    std::optional<Header> group_length = read_header(in, {true, false});

    if(group_length && group_length->tag == 0x00020000 && group_length->length == 4) {
        return {start, group_length->value_start + 4 + read_uint(in, 4, false)};
    }
    in.clear();
    in.seekg(static_cast<std::streamoff>(start));

    while(std::optional<Header> header = read_header(in, {true, false})) {
        if(header->tag >> 16 != 0x0002) {
            return {start, header->start};
        }
        skip_value(in, {true, false}, *header);
    }
    return {start, start};
}

std::optional<Dicom_util::File_range> Dicom_util::find_in_file(const std::filesystem::path& path,
    DcmObject& object, E_TransferSyntax xfer) {
    std::vector<DcmObject*> object_path;
    DcmObject* root = &object;

    for(; root->ident() != EVR_dataset && root->ident() != EVR_metainfo; root = root->getParent()) {
        if(root->getParent() == nullptr) {
            return std::nullopt;
        }
        object_path.insert(object_path.begin(), root);
    }
    const DcmXfer dcm_xfer(xfer);

    if(dcm_xfer.getStreamCompression() != ESC_none) {
        throw std::runtime_error("compressed (deflated) transfer syntax is not supported");
    }
    std::ifstream in(path, std::ios_base::binary);

    if(!in) {
        throw std::runtime_error("failed to open " + path.string());
    }
    const Region meta_region = find_meta_region(in);
    const bool in_meta = root->ident() == EVR_metainfo;

    if(object_path.empty()) {
        return in_meta && meta_region.end > meta_region.start
            ? std::optional<File_range>({meta_region.start, meta_region.end - meta_region.start})
            : std::nullopt;
    }
    Encoding encoding = in_meta
        ? Encoding{true, false}
        : Encoding{dcm_xfer.isExplicitVR() == OFTrue, dcm_xfer.isBigEndian() == OFTrue};
    Region region = in_meta ? meta_region : Region{meta_region.end, end_of_file};
    Header header;
    Encoding header_encoding = encoding;

    for(DcmObject* current : object_path) {
        const bool is_item = current->ident() == EVR_item;
        const uint32_t tag = is_item ? item_tag : current->getGTag() << 16 | current->getETag();
        std::optional<Header> found = find_child(in, encoding, region, tag, is_item ? get_index_nr(*current) : 0);

        if(!found) {
            return std::nullopt;
        }
        header = *found;
        header_encoding = encoding;
        encoding = nested_encoding(encoding, header);
        region = value_region(header);
    }
    in.seekg(static_cast<std::streamoff>(header.value_start));
    skip_value(in, header_encoding, header);
    return File_range{header.start, static_cast<uint64_t>(in.tellg()) - header.start};
}

std::vector<uint8_t> Dicom_util::read_file_bytes(const std::filesystem::path& path, uint64_t offset, size_t length) {
    std::ifstream in(path, std::ios_base::binary);
    in.seekg(static_cast<std::streamoff>(offset));
    std::vector<uint8_t> bytes(length);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length));

    if(!in) {
        throw std::runtime_error("failed to read " + path.string());
    }
    return bytes;
}

std::string Dicom_util::to_hex_dump(const std::vector<uint8_t>& bytes, uint64_t base_offset) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');

    for(size_t i = 0; i < bytes.size(); i += 16) {
        out << std::setw(8) << base_offset + i << "  ";
        std::string ascii;

        for(size_t j = i; j < i + 16; ++j) {
            if(j < bytes.size()) {
                out << std::setw(2) << static_cast<int>(bytes[j]) << ' ';
                ascii += bytes[j] >= 0x20 && bytes[j] < 0x7f ? static_cast<char>(bytes[j]) : '.';
            }
            else {
                out << "   ";
            }
        }
        out << ' ' << ascii << '\n';
    }
    return out.str();
}
