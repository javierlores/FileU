fmod: main.cpp filesystem.cpp
	g++ -o fmod main.cpp filesystem.cpp -std=c++11 -fpermissive -I.
clean:
	rm fmod
