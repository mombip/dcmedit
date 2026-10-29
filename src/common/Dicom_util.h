#pragma once
#include <cstddef>
#include <cstdint>
#include <dcmtk/dcmdata/dcobject.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Dicom_util
{
    void set_element(const std::string& tag_path, const std::string& value, bool create_if_needed, DcmObject&);
    void delete_element(const std::string& tag_path, DcmObject&);
    int get_index_nr(DcmObject&);

    struct File_range
    {
        uint64_t offset;
        uint64_t length;
    };
    // Locates the bytes of an element or item in the file the dataset was read from.
    // Returns nothing if the object isn't in the file (e.g. added after loading).
    std::optional<File_range> find_in_file(const std::filesystem::path&, DcmObject&, E_TransferSyntax);
    std::vector<uint8_t> read_file_bytes(const std::filesystem::path&, uint64_t offset, size_t length);
    std::string to_hex_dump(const std::vector<uint8_t>&, uint64_t base_offset = 0);
}
