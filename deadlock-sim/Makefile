CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99
SRC = src
OBJ = obj

ifeq ($(OS),Windows_NT)
  LDLIBS =
else
  CFLAGS += -pthread
  LDLIBS = -pthread
endif

COMMON = models detection bankers prevention recovery visualize export util
SIM_OBJS = $(patsubst %,$(OBJ)/%.o,main $(COMMON))
WL_OBJS  = $(patsubst %,$(OBJ)/%.o,workload rm threads $(COMMON))

all: simulator workload

simulator: $(SIM_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

workload: $(WL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(OBJ)/%.o: $(SRC)/%.c | $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ):
	mkdir -p $(OBJ)

clean:
	rm -rf $(OBJ) simulator workload simulator.exe workload.exe

demo: workload
	./workload

.PHONY: all clean demo
