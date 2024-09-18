all: libzmake.a zmake
	mkdir -p ~/bin/zmake_files/include
	mkdir -p ~/bin/zmake_files/lib
	cp zmake ~/bin/
	cp zmake.h zmake_helper.h zmake_util.h ~/bin/zmake_files/include/
	cp libzmake.a ~/bin/zmake_files/lib/
	cp -r demo ~/bin/zmake_files/

BUILD_main.o : BUILD_main.cpp
	g++ -std=c++17 -o $@ $< -g -Wall -c -D_GLIBCXX_DEBUG

zmake.o : zmake.cpp zmake.h zmake_util.h
	g++ -std=c++17 -o $@ $< -g -Wall -c -D_GLIBCXX_DEBUG

zmake : main.cpp zmake.o
	#g++ -std=c++17 -o $@ $^ -g -Wall -D_GLIBCXX_DEBUG -Wl,-no-as-needed -lpthread -Wl,-as-needed
	g++ -std=c++17 -o $@ $^ -g -Wall -D_GLIBCXX_DEBUG -lpthread

libzmake.a : zmake.o BUILD_main.o
	ar crs $@ $^

clean:
	rm -rf libzmake.a zmake.o BUILD_main.o zmake ~/bin/*zmake* BUILD .zmade zmake.dSYM

scp:
	rsync -r zmake.h zmake.cpp zmake_helper.h zmake_util.h Makefile main.cpp BUILD_main.cpp demo 78:~/zmake/
