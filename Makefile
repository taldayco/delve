# ===============================
# Makefile for delve.gdextension
# Godot 4.5 - Arch Linux (g++)
# ===============================
# ---- Project configuration ----
TARGET         := delve
OUTPUT_DIR     := bin
BUILD_TYPE     := debug
# ---- Compiler and linker ----
CXX            := g++
CXXFLAGS       := -std=c++20 -fPIC -O3 -Wall -Wextra -Wno-unknown-pragmas -Wno-unused-parameter -march=native
LDFLAGS        := -shared
INCLUDES := \
	-I./include \
	-I./godot-cpp \
	-I./godot-cpp/include \
	-I./godot-cpp/gen/include \
	-I./godot-cpp/gdextension
# ---- Godot CPP static lib ----
GODOT_CPP_LIB := ./godot-cpp/bin/libgodot-cpp.linux.template_debug.x86_64.a
# ---- Source directories ----
SRC_DIRS := ./src ./src/map
SRCS     := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.cpp))
OBJS     := $(SRCS:.cpp=.o)
# ---- Output shared library ----
TARGET_LIB := $(OUTPUT_DIR)/$(TARGET).linux.$(BUILD_TYPE).so
# ---- Default rule ----
all: $(TARGET_LIB)
	@echo "Build complete: $(TARGET_LIB)"
# ---- Build shared object ----
$(TARGET_LIB): $(OBJS)
	@mkdir -p $(OUTPUT_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(GODOT_CPP_LIB)
	@echo "Linked $(TARGET_LIB)"
# ---- Compile object files ----
%.o: %.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
# ---- Clean up build artifacts ----
clean:
	@echo "Cleaning..."
	rm -rf $(OBJS) $(TARGET_LIB)
	@echo "Done."
# ---- Convenience rebuild ----
rebuild: clean all
.PHONY: all clean rebuild
