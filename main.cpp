#include <iostream>

void run_cxl_enum();
void run_cxl_addr_map();
void run_cxl_io();
void run_cxl_mem();

static void print_menu() {
    std::cout << "\n===== CXL Test Menu =====\n"
              << "  [1]  CXL device discovery + DAX memory R/W test\n"
              << "  [2]  DRAM address map decode/encode test\n"
              << "  [3]  CXL.io  BAR register read\n"
              << "  [4]  CXL.mem DAX read/write verify\n"
              << "  [0]  Exit\n"
              << "Select: ";
}

int main() {
    int choice;
    do {
        print_menu();
        std::cin >> choice;
        switch (choice) {
        case 1: run_cxl_enum();     break;
        case 2: run_cxl_addr_map(); break;
        case 3: run_cxl_io();       break;
        case 4: run_cxl_mem();      break;
        case 0: std::cout << "EXIT.\n"; break;
        default: std::cout << "Invalid selection.\n"; break;
        }
    } while (choice != 0);
    return 0;
}
