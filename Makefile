CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wshadow -Wconversion -Wpedantic

SRC := $(wildcard compiler/*.cpp)
OBJ := $(patsubst compiler/%.cpp,build/%.o,$(SRC))

all: atomc

atomc: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

build:
	mkdir -p $@

build/%.o: compiler/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) atomc

.PHONY: all clean
