CXXFLAGS += $(INCLUDE) -std=c++0x
CXXFLAGS += -Wno-pmf-conversions -O0 -g

LDFLAGS += -lasmjit -L/usr/local/lib -g

INCLUDE += -I "/home/overmind/ref/binutil/asmjit-read-only/asmjit/src"

OBJECTS = main.o codegen.o parser.o object.o runtime.o

main : $(OBJECTS)
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o : %.cpp object.hpp codegen.hpp parser.hpp runtime.hpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

clean :
	rm -f main $(OBJECTS)
