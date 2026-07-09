CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Isupport
LDFLAGS ?=

TEST_SRCS = test_main.cpp test_edb.cpp test_edb_integrity.cpp test_api_compat.cpp support/unity.c
TARGET = test_edb
TARGET_107 = test_edb_107

.PHONY: all test test-107 fixtures clean

all: $(TARGET)

$(TARGET): $(TEST_SRCS) ../EDB.cpp
	$(CXX) $(CXXFLAGS) -I.. -DEDB_TEST $(TEST_SRCS) ../EDB.cpp -o $(TARGET) $(LDFLAGS)

$(TARGET_107): $(TEST_SRCS) ../release/1.0.7/EDB.cpp
	$(CXX) $(CXXFLAGS) -I../release/1.0.7 -DEDB_TEST $(TEST_SRCS) ../release/1.0.7/EDB.cpp -o $(TARGET_107) $(LDFLAGS)

fixtures:
	python ../tools/generate_fixtures.py

test: fixtures $(TARGET)
	./$(TARGET)

test-107: fixtures $(TARGET_107)
	./$(TARGET_107)

clean:
	rm -f $(TARGET) $(TARGET).exe $(TARGET_107) $(TARGET_107).exe
