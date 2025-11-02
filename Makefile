# ===== Makefile for macOS (Clang) =====

CXX ?= clang++
STD := -std=c++17
OPT ?= -O3
WARN := -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers

EIGEN_DIR ?=
ifeq ($(EIGEN_DIR),)
  ifneq ($(wildcard /opt/homebrew/include/eigen3/Eigen/Dense),)
    EIGEN_DIR := /opt/homebrew/include/eigen3
  else ifneq ($(wildcard /usr/local/include/eigen3/Eigen/Dense),)
    EIGEN_DIR := /usr/local/include/eigen3
  endif
endif
INCLUDES := -I$(EIGEN_DIR)

# OpenMP (optional)
# brew install libomp
OMPFLAGS :=
OMPLIBS  :=

HDRS := Topology.h TopologyDB.hpp TopoLineCompact.hpp Theory.h
SRCS_COMMON := Topology.cpp TopologyDB.cpp TopoLineCompact.cpp Tensor.C
OBJS_COMMON := $(SRCS_COMMON:.cpp=.o)

GEN_SRCS  := topology_generator.cpp
DECO_SRCS := decorate_generator.cpp
CLSF_SRCS := classify_topology.cpp

GEN_OBJS  := $(GEN_SRCS:.cpp=.o)
DECO_OBJS := $(DECO_SRCS:.cpp=.o)
CLSF_OBJS := $(CLSF_SRCS:.cpp=.o)

BINS := topology_generator decorate_generator classify_topology

CXXFLAGS := $(STD) $(OPT) $(WARN) $(INCLUDES) $(OMPFLAGS)
LDFLAGS  := $(OMPLIBS)

all: $(BINS)

topology_generator: $(GEN_OBJS) $(OBJS_COMMON)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

decorate_generator: $(DECO_OBJS) $(OBJS_COMMON)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

classify_topology: $(CLSF_OBJS) $(OBJS_COMMON)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS_COMMON) $(GEN_OBJS) $(DECO_OBJS) $(CLSF_OBJS)

distclean: clean
	rm -f $(BINS)

help:
	@echo "Usage:"
	@echo "  make                # build all"
	@echo "  make topology_generator"
	@echo "  make decorate_generator"
	@echo "  make classify_topology"
	@echo "  make clean"

