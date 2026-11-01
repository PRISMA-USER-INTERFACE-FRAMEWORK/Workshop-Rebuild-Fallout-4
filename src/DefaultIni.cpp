#include "PCH.h"

#include "DefaultIni.h"

namespace crafting::ini
{
    bool EnsureSettingsIni(const std::filesystem::path& a_path)
    {
        const auto pathUtf8 = a_path.u8string();
        const std::string pathText(pathUtf8.begin(), pathUtf8.end());
        std::string detail;
        const auto result = WriteDefaultIni(a_path, &detail);

        switch (result) {
        case WriteResult::kCreated:
            return true;
        case WriteResult::kAlreadyExists:
            return false;
        case WriteResult::kCheckFailed:
            logger::warn("PrismaCrafting: cannot check for '{}' ({}) -- not writing a default ini",
                         pathText, detail);
            return false;
        case WriteResult::kDirectoryFailed:
            logger::warn("PrismaCrafting: cannot create the folder for '{}' ({}) -- not writing a "
                         "default ini", pathText, detail);
            return false;
        case WriteResult::kWriteFailed:
            logger::warn("PrismaCrafting: could not write a default ini ({}). Nothing was left at "
                         "'{}', so the next launch will try again.", detail, pathText);
            return false;
        case WriteResult::kPublishFailed:
            logger::warn("PrismaCrafting: wrote a default ini but could not move it into place at "
                         "'{}' ({}). Nothing partial was left behind.", pathText, detail);
            return false;
        }
        return false;
    }
}
