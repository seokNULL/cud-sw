#include "../../include/cud/types.h"
#include "../../include/cud/ops.h"
#include <fstream>
#include <iomanip>
#include <iostream>

CudProgram& CudProgram::push(const std::vector<uint32_t>& new_insts) {
    if (new_insts.empty()) {
        std::cout << "[ERROR]: Tried to add empty instruction sequence\n";
        valid_ = false;
        return *this;
    }
    insts_.insert(insts_.end(), new_insts.begin(), new_insts.end());
    return *this;
}

CudProgram& CudProgram::add_end() {
    insts_.push_back(encode_end());
    return *this;
}

bool CudProgram::is_valid() const {
    return valid_;
}

void CudProgram::clear() {
    insts_.clear();
    valid_ = true;
}

const std::vector<uint32_t>& CudProgram::get_insts() const {
    return insts_;
}

void CudProgram::run() const {
    if (!valid_) {
        std::cout << "[ERROR]: CudProgram is invalid, not executing\n";
        return;
    }
    std::vector<uint32_t> final_insts = insts_;
    final_insts.push_back(encode_end());
    cud_print(final_insts);
}

void CudProgram::print_uops() const {
    if (!valid_) {
        std::cout << "[ERROR]: CudProgram is invalid, nothing to print\n";
        return;
    }

    std::ofstream out("uops_dump.txt");
    if (!out) {
        std::cout << "[ERROR]: Could not open uops_dump.txt for writing\n";
        return;
    }

    std::vector<uint32_t> final_insts = insts_;
    final_insts.push_back(encode_end());

    out << "=== CudProgram Micro-ops Dump ===\n";
    out << "Instruction count: " << final_insts.size() << "\n\n";
    for (size_t i = 0; i < final_insts.size(); i++) {
        out << "uop[" << std::setw(3) << i << "]: 0x"
            << std::hex << std::setw(8) << std::setfill('0') << final_insts[i]
            << std::dec << std::setfill(' ') << "\n";
    }
    out << "\n=== END ===\n";
}

void cud_print(const std::vector<uint32_t>& insts) {
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
