#pragma once

#include <string>
#include <vector>
#include <iostream>

struct UnitReport {
    std::string name;
    bool result;
    std::vector<UnitReport> report;

    UnitReport(const char* _name = "", bool _result = false) :name(_name), result(_result) {}
};

inline bool unit_test_valid(UnitReport &r) {
    if(r.report.size() > 0) {
        r.result=true;
        for(auto &cr: r.report) {
            bool b=unit_test_valid(cr);
            if(!b) {
                std::cout << "\033[31mUnitTest failed:\033[0m" << r.name << " of " << cr.name << std::endl;
            }
            r.result &=b;
        }
    }
    return r.result;
}

inline void print_uint_test(UnitReport &r, int depth = 0) {
    if (!r.name.empty()) {
        for (int i = 0; i < depth * 2; ++i) std::cout << ' ';
        if (r.result) {
            std::cout << r.name << "\033[32m [pass]\033[0m" << std::endl;
        }
        else {
            std::cout << r.name << "\033[31m [fail]\033[0m" << std::endl;
        }
    }
    if(r.report.size()>0) {
        for(auto &cr : r.report) {
            print_uint_test(cr, depth+1);
        }
    }
}

#define UNIT_TEST(name, result) {unit.report.push_back({name, result});}
