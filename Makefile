CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -Iinclude

SRCS = main.cpp \
       test/cxl_enum.cpp \
       test/cxl_addr.cpp \
       src/cxl/enumerator.cpp \
       src/cxl/address_map.cpp

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
