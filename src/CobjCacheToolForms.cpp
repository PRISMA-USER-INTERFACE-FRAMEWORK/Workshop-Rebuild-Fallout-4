#include "CobjCacheInternal.h"

#include <cctype>
#include <string>

namespace CobjCache
{
bool IsToolForm(RE::TESForm* a_form)
{
    // Constraint: GetItemCount is the only verified carry check, so every counted form is a tool candidate.
    return a_form != nullptr;
}
}
