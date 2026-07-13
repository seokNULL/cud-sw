#include <iostream>

void run_cxl_test();
void run_addr_map_test();

static void print_menu() {
    std::cout << "\n===== CXL Test Menu =====\n"
              << "  [1]  CXL device discovery + DAX memory R/W test\n"
              << "  [2]  DRAM address map decode/encode test\n"
              << "  [0]  Exit\n"
              << "Select: ";
}

int main() {
    int choice;
    do {
        print_menu();
        std::cin >> choice;
        switch (choice) {
        case 1: run_cxl_test();      break;
        case 2: run_addr_map_test(); break;
        case 0: std::cout << "Goodbye.\n"; break;
        default: std::cout << "Invalid selection.\n"; break;
        }
    } while (choice != 0);
    return 0;
}
