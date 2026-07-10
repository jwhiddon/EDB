CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Isupport
LDFLAGS ?=

TEST_SRCS = test_main.cpp test_dbs.cpp test_edb.cpp test_edb_integrity.cpp test_edb_edge.cpp test_api_compat.cpp \
  test_crypto.cpp test_crypto_header.cpp test_crypto_integrity.cpp test_crypto_blind.cpp test_compile_flags.cpp \
  support/unity.c
CRYPTO_SRCS = ../EDB_Crypto.cpp
TARGET = test_edb
TARGET_107 = test_edb_107
TARGET_CRYPTO = test_edb_crypto

.PHONY: all test test-107 test-crypto test-flags fixtures clean

all: $(TARGET)

$(TARGET): $(TEST_SRCS) ../EDB.cpp $(CRYPTO_SRCS)
	$(CXX) $(CXXFLAGS) -I.. -DEDB_TEST -DEDB_ENABLE_CRYPTO $(TEST_SRCS) ../EDB.cpp $(CRYPTO_SRCS) -o $(TARGET) $(LDFLAGS)

$(TARGET_107): $(TEST_SRCS) ../release/1.0.7/EDB.cpp
	$(CXX) $(CXXFLAGS) -I../release/1.0.7 -DEDB_TEST $(TEST_SRCS) ../release/1.0.7/EDB.cpp -o $(TARGET_107) $(LDFLAGS)

$(TARGET_CRYPTO): $(TEST_SRCS) ../EDB.cpp $(CRYPTO_SRCS)
	$(CXX) $(CXXFLAGS) -I.. -DEDB_TEST -DEDB_ENABLE_CRYPTO $(TEST_SRCS) ../EDB.cpp $(CRYPTO_SRCS) -o $(TARGET_CRYPTO) $(LDFLAGS)

fixtures:
	python ../tools/generate_fixtures.py

test: fixtures $(TARGET)
	./$(TARGET)

test-crypto: fixtures $(TARGET_CRYPTO)
	./$(TARGET_CRYPTO)

test-107: fixtures $(TARGET_107)
	./$(TARGET_107)

test-flags: fixtures $(TARGET)
	@echo "compile-flags matrix: default build includes EDB_ENABLE_CRYPTO in main test target"

clean:
	rm -f $(TARGET) $(TARGET).exe $(TARGET_107) $(TARGET_107).exe $(TARGET_CRYPTO) $(TARGET_CRYPTO).exe
