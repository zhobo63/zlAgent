#pragma once

#include <string>
#include <vector>
#include <iostream>

struct UnitReport {
    std::string name;
    bool result;
    std::vector<UnitReport> report;

    UnitReport(const std::string &_name = "", bool _result = false) :name(_name), result(_result) {}
};

inline bool unit_test_valid(UnitReport &r) {
    if(r.report.size() > 0) {
        r.result=true;
        for(auto &cr: r.report) {
            bool b=unit_test_valid(cr);
            r.result &=b;
        }
    }
    return r.result;
}

inline void log_unit_test(const std::string& name, bool result) {
    if (result) {
        std::cout << name << "\033[32m [pass]\033[0m" << std::endl; 
    }
    else {
        std::cout << name << "\033[31m [fail]\033[0m" << std::endl;
    }
}

inline void print_indent(int depth) {
    for (int i = 0; i < depth * 2; ++i) std::cout << ' ';
}

inline void print_uint_test(UnitReport &r, int depth = 0) {
    if (!r.name.empty()) {
        print_indent(depth);
        log_unit_test(r.name, r.result);
    }
    if(r.report.size()>0) {
        for(auto &cr : r.report) {
            print_uint_test(cr, depth+1);
        }
    }
}

inline void print_uint_test_fail(UnitReport& r, int depth = 0)
{
    if (r.result) {
        return;
    }    
    print_indent(depth);
    log_unit_test(r.name, r.result);
    for (auto& cr : r.report) {
        print_uint_test_fail(cr, depth + 1);
    }
}

#define UNIT_TEST(name, result) { \
    unit.report.push_back({name, result}); \
    log_unit_test(name, result); \
}
