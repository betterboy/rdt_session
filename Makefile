
all: predo depend lsocket test

CC = gcc
CXX = g++
CFLAGS := -fPIC -ggdb -Wall
CXXFLAGS := -fPIC -ggdb -Wall -std=c++17
OBJDIR = .obj

INCLUDES = -I./ -I/usr/local/include
SRC_C = mbuf.c rdt_session.c lsocket.c rdts_manager.c lrdt_client.c lrdt_server.c

SRC_LIST += $(SRC_C)
SRC = $(sort $(SRC_LIST))
OBJS = $(patsubst %.cc,%.o,$(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SRC))))
OBJ=$(addprefix $(OBJDIR)/,$(OBJS))
DEPS := $(OBJ:.o=.d)

$(OBJDIR)/%.o: %.c 
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ -c $<

$(OBJDIR)/%.d: %.c
	@$(CC) -MM -MG $(CFLAGS) $(INCLUDES) $< | sed -e 's,^\([^:]*\)\.o[ ]*:,$(@D)/\1.o $(@D)/\1.d:,' >$@

depend: $(DEPS)

.PHONY: predo test lsocket

lsocket: $(OBJ) $(SRC)
	gcc --shared -o lsocket.so $(OBJ)

predo:
	@test -d $(OBJDIR) || mkdir -p $(OBJDIR)

test: test.c mbuf.c rdt_session.c
	gcc -Wall -g3 -I ./ -o $@ $^

clean:
	rm test
	rm -rf .obj
	rm lsocket.so