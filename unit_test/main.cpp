#include "unit_test.h"

void test_example(UnitReport &parent) {
    UnitReport unit;
    unit.name="example";

    int a=1;
    int b=2;

    UNIT_TEST("test1", a == b);
    UNIT_TEST("test2", a == a);

    parent.report.push_back(unit);
}

void test_code_search_tools(UnitReport& parent);
void test_overview_tools(UnitReport& parent);
void test_file_tools(UnitReport& parent);

void main() {
    UnitReport main("UnitTest");

    try {
        //test_example(main);
        test_code_search_tools(main);
        test_file_tools(main);
        test_overview_tools(main);
    }
    catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "Unknown exception caught" << std::endl;
    }
    unit_test_valid(main);
    std::cout << "Report:" << std::endl;
    print_uint_test(main);

    if (main.result) {
        log_unit_test("all", true);
    }
}
