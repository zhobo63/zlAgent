#include "unit_test.h"

void test_code_search_tools(UnitReport& parent);
void test_config(UnitReport& parent);
void test_document_chunker(UnitReport& parent);
void test_embedding_provider(UnitReport& parent);
void test_file_tool(UnitReport& parent);
void test_fs_tool(UnitReport& parent);
void test_long_term_memory(UnitReport& parent);
void test_memory(UnitReport& parent);
void test_memory_tool(UnitReport& parent);
void test_overview_tool(UnitReport& parent);
void test_rag_tool(UnitReport& parent);
void test_rag_manager(UnitReport& parent);
void test_skill_system(UnitReport& parent);
void test_skill_tool(UnitReport& parent);
void test_vector_store(UnitReport& parent);

void main() {
#ifdef _WIN32
    // Set C runtime locale so std::cout handles multibyte (UTF-8) characters correctly.
    setlocale(LC_ALL, "zh_TW.UTF-8");
    // Set console input/output code pages to UTF-8 so emoji and all Unicode display correctly.
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
#endif

    UnitReport main("UnitTest");

    try {
        //test_example(main);
        test_code_search_tools(main);
        test_config(main);
        test_document_chunker(main);
        test_embedding_provider(main);
        test_file_tool(main);
        test_fs_tool(main);
        test_long_term_memory(main);
        test_memory(main);
        test_memory_tool(main);
        test_overview_tool(main);
        test_rag_tool(main);
        test_rag_manager(main);
        test_skill_system(main);
        test_skill_tool(main);
        test_vector_store(main);
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
    else {
        print_uint_test_fail(main);
    }
}
