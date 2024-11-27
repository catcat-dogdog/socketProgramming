SRC_DIR := src
OBJ_DIR := obj
INC_DIR := include

# 编译器和选项
CC      := gcc
CFLAGS  := -g -Wall 
CPPFLAGS := -I$(INC_DIR) -I$(SRC_DIR)

# all src files
SRC := $(wildcard $(SRC_DIR)/*.c)

# all binaries
BIN := example liso_server echo_client

# 默认目标
default: all
all : $(BIN)

example: $(OBJ_DIR)/y.tab.o \
         $(OBJ_DIR)/lex.yy.o \
         $(OBJ_DIR)/parse.o \
         $(OBJ_DIR)/example.o
	$(CC) $^ -o $@ $(LDFLAGS)

$(SRC_DIR)/lex.yy.c: $(SRC_DIR)/lexer.l
	flex -o $@ $^

$(SRC_DIR)/y.tab.c: $(SRC_DIR)/parser.y
	yacc -d $^
	mv y.tab.c $@
	mv y.tab.h $(SRC_DIR)/y.tab.h

liso_server: $(OBJ_DIR)/echo_server.o \
             $(OBJ_DIR)/client_handler.o \
             $(OBJ_DIR)/logger.o \
             $(OBJ_DIR)/request_queue.o \
             $(OBJ_DIR)/http_response.o \
             $(OBJ_DIR)/y.tab.o \
             $(OBJ_DIR)/lex.yy.o \
             $(OBJ_DIR)/parse.o
	$(CC) $^ -o $@ $(LDFLAGS)

echo_client: $(OBJ_DIR)/echo_client.o \
             $(OBJ_DIR)/logger.o
	$(CC) $^ -o $@ $(LDFLAGS)

# 编译规则
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# 创建目标目录
$(OBJ_DIR):
	mkdir -p $@

clean:
	$(RM) $(OBJ_DIR)/*.o $(BIN) $(SRC_DIR)/lex.yy.c $(SRC_DIR)/y.tab.* *.log
	$(RM) -r $(OBJ_DIR)
