#include "pch.h"
#include "unit_test.h"


// ── Entry point ────────────────────────────────────────────────

void test_code_search_tools(UnitReport& parent)
{
    UnitReport unit("code_search_tools");
    LOG_INFO("test_code_search_tools", "code_search_tools");

    parent.report.push_back(unit);
}
