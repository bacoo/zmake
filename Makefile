all: libzmake.a zmake
	mkdir -p ~/bin/zmake_files/include
	mkdir -p ~/bin/zmake_files/lib
	cp zmake ~/bin/
	cp zmake.h zmake_helper.h zmake_util.h ~/bin/zmake_files/include/
	cp libzmake.a ~/bin/zmake_files/lib/
	cp -r demo ~/bin/zmake_files/

LINK_PTHREAD := -Wl,-no-as-needed -lpthread -Wl,-as-needed
ifeq ($(shell uname), Darwin)
	LINK_PTHREAD := -lpthread
endif

BUILD_main.o : BUILD_main.cpp zmake.h  zmake_helper.h  zmake_util.h
	g++ -std=c++17 -o $@ $< -g -Wall -c -D_GLIBCXX_DEBUG

zmake.o : zmake.cpp zmake.h zmake_helper.h zmake_util.h
	g++ -std=c++17 -o $@ $< -g -Wall -c -D_GLIBCXX_DEBUG

zmake : main.cpp zmake.o
	g++ -std=c++17 -o $@ $^ -g -Wall -D_GLIBCXX_DEBUG $(LINK_PTHREAD)

libzmake.a : zmake.o BUILD_main.o
	ar crs $@ $^

clean:
	rm -rf libzmake.a zmake.o BUILD_main.o zmake .zmade zmake.dSYM
