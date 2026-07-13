#include <iostream>

void run_cxl_enum();
void run_cxl_addr_map();

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
        case 1: run_cxl_enum();      break;
        case 2: run_cxl_addr_map(); break;
        case 0: std::cout << "EXIT.\n"; break;
        default: std::cout << "Invalid selection.\n"; break;
        }
    } while (choice != 0);
    return 0;
}
