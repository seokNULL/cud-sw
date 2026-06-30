#include "include/instruction.h"
#include <iostream>
#include <fstream>
#include <iomanip>

CuDProgram& CuDProgram::push(const std::vector<uint32_t>& new_insts) {
    if (new_insts.empty()) {
        std::cout << "[ERROR]: Tried to add empty instruction sequence\n";
        valid = false;
        return *this;
    }

    insts.insert(insts.end(), new_insts.begin(), new_insts.end());
    return *this;
}

CuDProgram& CuDProgram::add_end() {
    insts.push_back(END());
    return *this;
}

bool CuDProgram::is_valid() const {
    return valid;
}

void CuDProgram::clear() {
    insts.clear();
    valid = true;
}

const std::vector<uint32_t>& CuDProgram::get_insts() const {
    return insts;
}

void CuDProgram::run() const {
    if (!valid) {
        std::cout << "[ERROR]: CuDProgram is invalid, not executing\n";
        return;
    }

    std::vector<uint32_t> final_insts = insts;
    final_insts.push_back(END());
    cud_print(final_insts);
}

void CuDProgram::print_uops() const {
    if (!valid) {
        std::cout << "[ERROR]: CuDProgram is invalid, nothing to print\n";
        return;
    }

    std::ofstream out("uops_dump.txt");
    if (!out) {
        std::cout << "[ERROR]: Could not open uops_dump.txt for writing\n";
        return;
    }

    std::vector<uint32_t> final_insts = insts;
    final_insts.push_back(END());

    out << "=== CuDProgram Micro-ops Dump ===\n";
    out << "Instruction count: " << final_insts.size() << "\n\n";

    for (size_t i = 0; i < final_insts.size(); i++) {
        out << "uop[" << std::setw(3) << i << "]: 0x"
            << std::hex << std::setw(8) << std::setfill('0') << final_insts[i]
            << std::dec << std::setfill(' ') << "\n";
    }

    out << "\n=== END ===\n";
}


void cud_print(const std::vector<uint32_t>& insts)
{
    if (insts.empty()) {
        std::cout << "[INFO]: No instructions to execute" << std::endl;
        return;
    }

    std::cout << "[CuD EXEC] Instruction count = " << insts.size() << std::endl;
    for (auto inst : insts) {
        std::cout << "0x"
                  << std::hex << std::setw(8) << std::setfill('0')
                  << inst << std::endl;
    }
    std::cout << std::dec << std::setfill(' ');
}
