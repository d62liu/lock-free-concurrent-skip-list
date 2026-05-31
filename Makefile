CXX = g++
CXXFLAGS = -std=c++17 -O2 -pthread -Wall

benchmark: benchmark.cpp skip_list_sequential.h skip_list_locked.h skip_list_lockfree.h
	$(CXX) $(CXXFLAGS) benchmark.cpp -o benchmark

run: benchmark
	./benchmark

clean:
	rm -rf benchmark *.o *.dSYM

.PHONY: run clean
