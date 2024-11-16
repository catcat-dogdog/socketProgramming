SRC_DIR := src
OBJ_DIR := obj
# all src files
SRC := $(wildcard $(SRC_DIR)/*.c)
# all objects
OBJ := $(OBJ_DIR)/y.tab.o $(OBJ_DIR)/lex.yy.o $(OBJ_DIR)/parse.o $(OBJ_DIR)/example.o $(OBJ_DIR)/logger.o
# all binaries
BIN := example liso_server echo_client
# C compiler
CC  := gcc
# C PreProcessor Flag
CPPFLAGS := -Iinclude
# compiler flags
CFLAGS   := -g -Wall
# 添加 pthread 库
LDFLAGS := -pthread

default: all
all : example liso_server echo_client

example: $(OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

$(SRC_DIR)/lex.yy.c: $(SRC_DIR)/lexer.l
	flex -o $@ $^

$(SRC_DIR)/y.tab.c: $(SRC_DIR)/parser.y
	yacc -d $^
	mv y.tab.c $@
	mv y.tab.h $(SRC_DIR)/y.tab.h

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

liso_server: $(OBJ_DIR)/y.tab.o $(OBJ_DIR)/lex.yy.o $(OBJ_DIR)/parse.o $(OBJ_DIR)/echo_server.o $(OBJ_DIR)/logger.o
	$(CC) -Werror $^ -o $@ $(LDFLAGS)

echo_client: $(OBJ_DIR)/echo_client.o
	$(CC) -Werror $^ -o $@ $(LDFLAGS)

$(OBJ_DIR):
	mkdir $@

clean:
	$(RM) $(OBJ) $(BIN) $(SRC_DIR)/lex.yy.c $(SRC_DIR)/y.tab.* *.log
	$(RM) -r $(OBJ_DIR)

#nothing to do