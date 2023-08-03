# Implementation and Testing of GBDI Memory Compression Algorithm

This is a C program that demonstrates memory compression using the GBDI compression algorithm on given memory dump files in the ELF format. The program reads an ELF file containing LLC memory dump data, extracts cache lines from the loadable segments, and compresses them using the GBDI compression algorithm. The original size and compressed size of the cache lines are then printed, along with the compression ratio.


## How to run *gbdi_compression.c*

To compile the program use the following command

```sh
gcc -o gbdi_compression gbdi_compression.c
```

To run the program use the following command, where *filename* is the memory dump file

```sh
./gbdi_compression [path/filename]
```

## Functions

The program includes the following functions:

- *delta_compare()*: A comparator function used for sorting the deltas.
- *establish_global_base_set_hb()*: Establishes the global base set using the most common deltas.
- *write_variable_length_integer()*: Writes an integer in variable length format to a buffer.
- *gbdi_compress()*: Compresses a sequence of cache lines using the GBDI compression algorithm.
- *extract_cache_lines()*: Extracts cache lines from a set of memory segments.
- *main()*:  The main entry point of the program, which reads the ELF file, extracts cache lines, compresses them, and prints the results.

## Dependencies

The program depends on the following system header files:

- elf.h
- fcntl.h
- stdio.h
- stdlib.h
- string.h
- sys/mman.h
- sys/stat.h
- sys/types.h
- unistd.h
