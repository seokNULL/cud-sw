CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -Iinclude

SRCS = main.cpp \
       test/utils.cpp \
       test/cxl_enum.cpp \
       test/cxl_addr.cpp \
       test/cxl_io.cpp \
       test/cxl_mem.cpp \
       test/cud_interface.cpp \
       test/cud_library.cpp \
       src/cxl/enumerator.cpp \
       src/cxl/address_map.cpp \
       src/cxl/io.cpp \
       src/cxl/mem.cpp \
       src/cud/interface.cpp \
       src/cud/instruction.cpp \
       src/cud/compute_lib/data_copy.cpp \
       src/cud/compute_lib/logical.cpp

OBJS   = $(SRCS:.cpp=.o)
TARGET = cxl_runner

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
