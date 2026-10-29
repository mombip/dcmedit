#include "ui/open_files_dialog/Open_files_presenter.h"

#include "logging/Log.h"
#include "models/Dicom_files.h"
#include "ui/open_files_dialog/IOpen_files_view.h"
#include "ui/progressbar/Progress_presenter.h"

#include <eventi/Scoped_defer.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

Open_files_presenter::Open_files_presenter(IOpen_files_view& view, Dicom_files& dicom_files)
    : m_view(view),
      m_dicom_files(dicom_files) {}

void Open_files_presenter::show_dialog() {
    open_files(m_view.show_file_dialog());
}

void Open_files_presenter::open_files(const std::vector<fs::path>& paths) {
    if (paths.empty()) {
        return;
    }
    std::vector<std::string> file_errors;
    eventi::Scoped_defer defer(m_dicom_files.current_file_set);
    std::unique_ptr<IProgress_view> progress_view = m_view.create_progress_view();
    Progress_presenter progress_presenter(*progress_view, "Opening files");
    auto thread_func = [&] {
        // Only errors for explicitly chosen files are shown; folders usually contain non-DICOM files too.
        std::vector<std::pair<fs::path, bool>> file_paths;
        for(const fs::path& path : paths) {
            if(fs::is_directory(path)) {
                const auto options = fs::directory_options::skip_permission_denied;
                for(const fs::directory_entry& entry : fs::recursive_directory_iterator(path, options)) {
                    if(entry.is_regular_file()) {
                        file_paths.emplace_back(entry.path(), false);
                    }
                }
            }
            else {
                file_paths.emplace_back(path, true);
            }
        }
        progress_presenter.set_max_progress(static_cast<int>(file_paths.size()));
        for(const auto& [file_path, show_error] : file_paths) {
            if(progress_presenter.cancelled()) {
                break;
            }
            try {
                m_dicom_files.open_file(file_path);
            }
            catch(const std::exception& e) {
                const std::string error = "Failed to open file: " + file_path.string() +
                    "\nReason: " + std::string(e.what());
                if(show_error) {
                    file_errors.push_back(error);
                }
                else {
                    Log::error(error);
                }
            }
            progress_presenter.increment_progress();
        }
        progress_presenter.close();
    };
    progress_presenter.execute(thread_func);

    if(!file_errors.empty()) {
        m_view.show_error(file_errors);
    }
}
