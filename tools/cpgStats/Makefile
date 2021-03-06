#==================================================================================================
# PROJECT: gemBS
# FILE: Makefile
# DATE: 07/04/2017
# AUTHOR(S): Marcos Fernandez <mfernandez@cnag.crg.eu>
# DESCRIPTION: Builds the external tools for gemBS
#==================================================================================================

CC = gcc
CFLAGS = -g -O3 -Wall -c -fmessage-length=0 -MMD -MP
DEBUG_FLAGS = -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP
LIBS = -lm -lz

FOLDER_BIN = ../bin
TOOLS = cpgStats
TOOLS_BIN = $(addprefix $(FOLDER_BIN)/, $(TOOLS))

INPUTS = common counts intersection main methBed parsArgs parseInput
TOOLS_OBJ = $(addsuffix .o, $(INPUTS))

default: all

# object files
all: TOOLS_FLAGS = $(CFLAGS)
all:  $(TOOLS_OBJ) 
	$(CC) -o $(TOOLS_BIN) $(TOOLS_OBJ) $(LIBS)

static: TOOLS_FLAGS = $(CFLAGS) -static
static: $(TOOLS_OBJ) 
	$(CC) -o $(TOOLS_BIN) $(TOOLS_OBJ) $(LIBS)

debug: TOOLS_FLAGS = $(DEBUG_FLAGS)
debug:  $(TOOLS_OBJ) 
	$(CC) -o $(TOOLS_BIN) $(TOOLS_OBJ) $(LIBS)

counts.o: 
	$(CC) $(TOOLS_FLAGS) -o counts.o counts.c

intersection.o:
	$(CC) $(TOOLS_FLAGS) -o intersection.o intersection.c

main.o:
	$(CC) $(TOOLS_FLAGS) -o main.o main.c

methBed.o:
	$(CC) $(TOOLS_FLAGS) -o methBed.o methBed.c

parsArgs.o:
	$(CC) $(TOOLS_FLAGS) -o parsArgs.o parsArgs.c

parseInput.o:
	$(CC) $(TOOLS_FLAGS) -o parseInput.o parseInput.c

clean: 
	$(RM) $(TOOLS_BIN)  *.o

 
