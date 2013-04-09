CXXFLAGS += $(INCLUDE) -std=c++0x -Wno-pmf-conversions -O0 -g -Wall

LDFLAGS += -lasmjit -L/usr/local/lib -g

INCLUDE += -I "/home/overmind/ref/binutil/asmjit-read-only/asmjit/src"

OBJECTS = main.o parser.o object.o runtime.o gc.o util.o codegen2.o

HEADERS = object.hpp parser.hpp runtime.hpp util.hpp gc.hpp codegen2.hpp

main : $(OBJECTS)
	$(CXX) $^ -o $@ $(LDFLAGS)

test-gc : test-gc.o gc.o object.o util.o codegen2.o
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o : %.cpp $(HEADERS)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

clean :
	rm -f main $(OBJECTS)

