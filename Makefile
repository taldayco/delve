# ===============================
# Makefile for Map Generator
# Standalone C++20 - GLFW
# ===============================

# ---- Project configuration ----
TARGET         := map_gen
OUTPUT_DIR     := bin
BUILD_TYPE     := debug

# ---- Compiler and linker ----
CXX            := g++
CXXFLAGS       := -std=c++20 -O3 -Wall -Wextra -Wno-unused-parameter -march=native
LDFLAGS        := 
LIBS           := -lglfw

# ---- Debug vs Release ----
ifeq ($(BUILD_TYPE),debug)
    CXXFLAGS += -g -DDEBUG
else
    CXXFLAGS += -DNDEBUG
endif

# ---- Includes ----
INCLUDES := -I./include

# ---- Source directories ----
SRC_DIRS := ./src ./src/map
SRCS     := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.cpp))
OBJS     := $(SRCS:.cpp=.o)

# ---- Output executable ----
TARGET_BIN := $(OUTPUT_DIR)/$(TARGET)

# ---- Default rule ----
all: $(TARGET_BIN)
	@echo "Build complete: $(TARGET_BIN)"

# ---- Build executable ----
$(TARGET_BIN): $(OBJS)
	@mkdir -p $(OUTPUT_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	@echo "Linked $(TARGET_BIN)"

# ---- Compile object files ----
%.o: %.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ---- Clean up build artifacts ----
clean:
	@echo "Cleaning..."
	rm -rf $(OBJS) $(TARGET_BIN)
	@echo "Done."

# ---- Convenience rebuild ----
rebuild: clean all

# ---- Run the program ----
run: $(TARGET_BIN)
	@./$(TARGET_BIN)

.PHONY: all clean rebuild run
