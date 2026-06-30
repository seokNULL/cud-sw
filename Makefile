# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -I. -I./CuD_sim

# Target executable name
TARGET = sim_test.exe
HW_MIN_TARGET = cud_hw_runner_min.exe
MEM_ONLY_TARGET = cxl_mem_only_runner.exe
U200_IO_TARGET = u200_io_runner
ROWCOPY_PROBE_TARGET = cxl_rowcopy_probe
CXL_HW_TEST_TARGET = cxl_hw_test
ROWCOPY_SWEEP_TEST_TARGET = cxl_rowcopy_sweep_test
NOT_MASK_PROBE_TARGET = cxl_not_mask_probe

# Directories
SIM_DIR = CuD_sim
include_DIR = include

# Source files
# Added all .cpp files from root and the specific ones from CuD_sim
SRCS = all_supported_functions.cpp \
       cxl_driver.cpp \
       cud_program.cpp \
       find_dont_care.cpp \
       utils.cpp \
       $(SIM_DIR)/simulator.cpp \
       $(SIM_DIR)/sim_test.cpp

HW_MIN_SRCS = all_supported_functions.cpp \
              cxl_driver.cpp \
              cud_program.cpp \
              find_dont_care.cpp \
              utils.cpp \
              cud_hw_runner_min.cpp

MEM_ONLY_SRCS = cxl_driver.cpp \
                cxl_mem_only_helper.cpp \
                cxl_mem_only_runner.cpp

U200_IO_SRCS = cxl_driver.cpp \
               u200_io_runner.cpp

ROWCOPY_PROBE_SRCS = cxl_driver.cpp \
                     cxl_rowcopy_probe.cpp

CXL_HW_TEST_SRCS = all_supported_functions.cpp \
                   cxl_driver.cpp \
                   cxl_hardware.cpp \
                   cud_program.cpp \
                   find_dont_care.cpp \
                   utils.cpp \
                   cxl_hw_test.cpp

ROWCOPY_SWEEP_TEST_SRCS = all_supported_functions.cpp \
                          cxl_driver.cpp \
                          cxl_hardware.cpp \
                          cud_program.cpp \
                          find_dont_care.cpp \
                          utils.cpp \
                          Testing_files/cxl_hw_test.cpp

NOT_MASK_PROBE_SRCS = all_supported_functions.cpp \
                      cxl_driver.cpp \
                      cxl_hardware.cpp \
                      cud_program.cpp \
                      find_dont_care.cpp \
                      utils.cpp \
                      Testing_files/not_mask_probe.cpp

# Object files (converts .cpp to .o)
OBJS = $(SRCS:.cpp=.o)
HW_MIN_OBJS = $(HW_MIN_SRCS:.cpp=.o)
MEM_ONLY_OBJS = $(MEM_ONLY_SRCS:.cpp=.o)
U200_IO_OBJS = $(U200_IO_SRCS:.cpp=.o)
ROWCOPY_PROBE_OBJS = $(ROWCOPY_PROBE_SRCS:.cpp=.o)
CXL_HW_TEST_OBJS = $(CXL_HW_TEST_SRCS:.cpp=.o)
ROWCOPY_SWEEP_TEST_OBJS = $(ROWCOPY_SWEEP_TEST_SRCS:.cpp=.o)
NOT_MASK_PROBE_OBJS = $(NOT_MASK_PROBE_SRCS:.cpp=.o)

# Header dependencies (to ensure re-build on header change)
DEPS = $(include_DIR)/instruction.h $(include_DIR)/cxl_driver.h $(SIM_DIR)/simulator.h

# Default rule
all: $(TARGET)
hw_runner_min: $(HW_MIN_TARGET)
mem_runner: $(MEM_ONLY_TARGET)
io_runner: $(U200_IO_TARGET)
rowcopy_probe: $(ROWCOPY_PROBE_TARGET)
hw_test: $(CXL_HW_TEST_TARGET)
rowcopy_sweep_test: $(ROWCOPY_SWEEP_TEST_TARGET)
not_mask_probe: $(NOT_MASK_PROBE_TARGET)

# Link objects to create the executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

$(HW_MIN_TARGET): $(HW_MIN_OBJS)
	$(CXX) $(CXXFLAGS) -o $(HW_MIN_TARGET) $(HW_MIN_OBJS)

$(MEM_ONLY_TARGET): $(MEM_ONLY_OBJS)
	$(CXX) $(CXXFLAGS) -o $(MEM_ONLY_TARGET) $(MEM_ONLY_OBJS)

$(U200_IO_TARGET): $(U200_IO_OBJS)
	$(CXX) $(CXXFLAGS) -o $(U200_IO_TARGET) $(U200_IO_OBJS)

$(ROWCOPY_PROBE_TARGET): $(ROWCOPY_PROBE_OBJS)
	$(CXX) $(CXXFLAGS) -o $(ROWCOPY_PROBE_TARGET) $(ROWCOPY_PROBE_OBJS)

$(CXL_HW_TEST_TARGET): $(CXL_HW_TEST_OBJS)
	$(CXX) $(CXXFLAGS) -o $(CXL_HW_TEST_TARGET) $(CXL_HW_TEST_OBJS)

$(ROWCOPY_SWEEP_TEST_TARGET): $(ROWCOPY_SWEEP_TEST_OBJS)
	$(CXX) $(CXXFLAGS) -o $(ROWCOPY_SWEEP_TEST_TARGET) $(ROWCOPY_SWEEP_TEST_OBJS)

$(NOT_MASK_PROBE_TARGET): $(NOT_MASK_PROBE_OBJS)
	$(CXX) $(CXXFLAGS) -o $(NOT_MASK_PROBE_TARGET) $(NOT_MASK_PROBE_OBJS)

# Compile source files to object files
%.o: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(OBJS) $(HW_MIN_OBJS) $(MEM_ONLY_OBJS) $(U200_IO_OBJS) $(ROWCOPY_PROBE_OBJS) $(CXL_HW_TEST_OBJS) $(ROWCOPY_SWEEP_TEST_OBJS) $(NOT_MASK_PROBE_OBJS) $(TARGET) $(HW_MIN_TARGET) $(MEM_ONLY_TARGET) $(U200_IO_TARGET) $(ROWCOPY_PROBE_TARGET) $(CXL_HW_TEST_TARGET) $(ROWCOPY_SWEEP_TEST_TARGET) $(NOT_MASK_PROBE_TARGET)

.PHONY: all hw_runner_min mem_runner io_runner rowcopy_probe hw_test rowcopy_sweep_test not_mask_probe clean