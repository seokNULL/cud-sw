CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -Iinclude

CUD_SRCS = src/cud/address.cpp \
           src/cud/ra_group.cpp \
           src/cud/row_alloc.cpp \
           src/cud/program.cpp \
           src/cud/ops_basic.cpp \
           src/cud/ops_logic.cpp \
           src/cud/ops_arith.cpp \
           src/cud/ops_mult.cpp

CXL_SRCS = src/cxl/driver.cpp \
           src/cxl/device.cpp \
           src/cxl/enumerator.cpp

COMMON_SRCS = $(CUD_SRCS) $(CXL_SRCS)

MAIN_TARGET = cud_runner
HW_TARGET   = cud_hw_test

MAIN_SRCS   = main.cpp test/cxl_test.cpp $(COMMON_SRCS)
HW_SRCS     = test/hw_test.cpp $(COMMON_SRCS)

MAIN_OBJS   = $(MAIN_SRCS:.cpp=.o)
HW_OBJS     = $(HW_SRCS:.cpp=.o)

.PHONY: all hw_test clean

all: $(MAIN_TARGET)

hw_test: $(HW_TARGET)

$(MAIN_TARGET): $(MAIN_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(HW_TARGET): $(HW_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(MAIN_OBJS) $(HW_OBJS) $(MAIN_TARGET) $(HW_TARGET)
