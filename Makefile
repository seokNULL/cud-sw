CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O3 -march=native -fopenmp -Iinclude
BUILDDIR = build

SRCS = main.cpp \
       test/utils.cpp \
       test/cxl_enum.cpp \
       test/cxl_addr.cpp \
       test/cxl_io.cpp \
       test/cxl_mem.cpp \
       test/cud_interface.cpp \
       test/cud_compute.cpp \
       test/fault_search.cpp \
       test/inst_trace.cpp \
       test/bench.cpp \
       src/cxl/enumerator.cpp \
       src/cxl/address_map.cpp \
       src/cxl/io.cpp \
       src/cxl/mem.cpp \
       src/cud/interface.cpp \
       src/cud/instruction.cpp \
       src/cud/compute_lib/data_copy.cpp \
       src/cud/compute_lib/logical.cpp \
       src/cud/compute_lib/data_mapper.cpp \
       src/cud/compute_lib/add.cpp \
       src/cud/compute_lib/fa6.cpp \
       src/cud/compute_lib/mul.cpp \
       src/cud/compute_lib/gemv.cpp

OBJS   = $(SRCS:%.cpp=$(BUILDDIR)/%.o)
TARGET = cud-cxl-run

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lgomp

$(BUILDDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR) $(TARGET)
