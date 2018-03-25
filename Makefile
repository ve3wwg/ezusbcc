CXX	= g++

STD	= -std=c++11

.cpp.o:
	$(CXX) -Wall -c -g $(STD) $< -o $*.o

ezusbcc: ezusbcc.o 
	$(CXX) $(STD) ezusbcc.o -o ezusbcc

clean:
	rm -f *.o 

clobber: clean
	rm -f ezusbcc

test::	ezusbcc
	./ezusbcc <testwave.wvf

