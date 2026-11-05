# ============================================
# LMS v4.0 — Linux Makefile
# 一键编译：make && ./lms-server
# ============================================
CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic
INCLUDES := -I include
LDFLAGS  := -pthread

# 自动检测 MySQL
HAS_MYSQL := $(shell pkg-config --exists mysqlclient && echo 1 || echo 0)
ifeq ($(HAS_MYSQL),1)
CXXFLAGS +=  -DLMS_HAS_MYSQL
INCLUDES +=  $(shell pkg-config --cflags mysqlclient)
LDFLAGS  += $(shell pkg-config --libs mysqlclient)
MYSQL_SRC =  src/user_repo_mysql.cpp
endif

SRCS := src/main.cpp \
        src/user_repo_file.cpp \
        src/order_repo_file.cpp \
        src/user_service.cpp \
        $(MYSQL_SRC)

TARGET := lms-server

.PHONY: all clean run test

all: $(TARGET) lms-storage

$(TARGET): $(SRCS) $(wildcard include/lms/*.h)
	@echo "  CXX  $(TARGET)"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(TARGET) $(LDFLAGS)
	@echo "  编译完成 → ./$(TARGET)"

lms-storage: src/storage_tool.cpp $(wildcard include/lms/*.h)
	$(CXX) $(CXXFLAGS) $(INCLUDES) src/storage_tool.cpp -o lms-storage -pthread

run: $(TARGET)
	./$(TARGET) --config config/learning.json

clean:
	rm -f $(TARGET) server-lifecycle lms-storage

test: $(TARGET) lms-storage
	@mkdir -p .tmp
	TMPDIR=$(CURDIR)/.tmp python3 tests/stage1_http.py ./$(TARGET)
	TMPDIR=$(CURDIR)/.tmp python3 tests/startup_config.py ./$(TARGET)
	TMPDIR=$(CURDIR)/.tmp python3 tests/storage_http.py ./$(TARGET) ./lms-storage
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/server_lifecycle.cpp -o server-lifecycle -pthread
	python3 tests/check_lifecycle.py ./server-lifecycle

# 帮助
help:
	@echo "make        编译项目"
	@echo "make run    编译并运行"
	@echo "make test   冒烟测试"
	@echo "make clean  清理编译产物"
