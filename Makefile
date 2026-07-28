CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -Iinclude
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
       src/cxl/enumerator.cpp \
       src/cxl/address_map.cpp \
       src/cxl/io.cpp \
       src/cxl/mem.cpp \
       src/cud/interface.cpp \
       src/cud/instruction.cpp \
       src/cud/compute_lib/data_copy.cpp \
       src/cud/compute_lib/logical.cpp \
       src/cud/compute_lib/data_mapper.cpp \
       src/cud/compute_lib/inst_gen.cpp \
       src/cud/compute_lib/add_table.cpp

OBJS   = $(SRCS:%.cpp=$(BUILDDIR)/%.o)
TARGET = cud-cxl-run

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILDDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR) $(TARGET)
