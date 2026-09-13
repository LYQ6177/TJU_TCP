TOP_DIR = .
INC_DIR = $(TOP_DIR)/inc
SRC_DIR = $(TOP_DIR)/src
BUILD_DIR = $(TOP_DIR)/build

CC=gcc
FLAGS = -pthread -g -ggdb -DDEBUG -I$(INC_DIR)
OBJS = $(BUILD_DIR)/tju_packet.o \
	   $(BUILD_DIR)/kernel.o \
	   $(BUILD_DIR)/tju_tcp.o 

.PHONY: default all clean server client dirs

default: all

all: dirs server client

dirs:
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | dirs
	$(CC) $(FLAGS) -c -o $@ $<

clean:
	-rm -f $(BUILD_DIR)/*.o client server

server: dirs $(OBJS)
	$(CC) $(FLAGS) ./src/server.c -o server $(OBJS)

client: dirs $(OBJS)
	$(CC) $(FLAGS) ./src/client.c -o client $(OBJS)
