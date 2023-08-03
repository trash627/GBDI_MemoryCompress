#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_BASES 2
#define CACHE_LINE_SIZE 32

typedef struct {
    uint64_t *bases;
    size_t count;
} GlobalBaseSet;

typedef struct {
    uintptr_t address;
    size_t size;
    uint8_t *data;
} Segment;

int delta_compare(const void *a, const void *b) {
    const uint64_t *delta_a = (const uint64_t *)a;
    const uint64_t *delta_b = (const uint64_t *)b;
    return (*delta_a > *delta_b) - (*delta_a < *delta_b);
}


void establish_global_base_set_hb(const uint64_t *data, size_t data_count, GlobalBaseSet *global_base_set, size_t max_bases) {
   
    global_base_set->count = 0;
    global_base_set->bases = malloc(sizeof(uint64_t) * max_bases);

    if (data_count < 2) {
        return;
    }

    size_t delta_count = data_count - 1;
    uint64_t *deltas = malloc(sizeof(uint64_t) * delta_count);

    for (size_t i = 0; i < delta_count; ++i) {
        deltas[i] = data[i + 1] - data[i];
    }

    
    qsort(deltas, delta_count, sizeof(uint64_t), delta_compare);

    
    size_t current_delta_count = 1;
    size_t max_delta_count = 1;
    uint64_t current_delta = deltas[0];
    uint64_t max_delta = deltas[0];
    for (size_t i = 1; i < delta_count && global_base_set->count < max_bases; ++i) {
        if (deltas[i] == current_delta) {
            current_delta_count++;
        } else {
            if (current_delta_count > max_delta_count) {
                max_delta_count = current_delta_count;
                max_delta = current_delta;
                global_base_set->bases[global_base_set->count++] = max_delta;
            }

            current_delta_count = 1;
            current_delta = deltas[i];
        }

        if (i == delta_count - 1 && current_delta_count > max_delta_count) {
            max_delta_count = current_delta_count;
            max_delta = current_delta;
            global_base_set->bases[global_base_set->count++] = max_delta;
        }
    }

    free(deltas);
}

void write_variable_length_integer(uint64_t value, uint8_t **output_buffer) {
    while (value >= 0x80) {
        **output_buffer = (value & 0x7F) | 0x80;
        (*output_buffer)++;
        value >>= 7;
    }
    **output_buffer = value & 0x7F;
    (*output_buffer)++;
}

void gbdi_compress(const uint64_t *data, size_t data_count, const GlobalBaseSet *global_base_set, uint8_t *output_buffer, size_t *output_size) {
    uint8_t *buffer_start = output_buffer;

    for (size_t i = 0; i < data_count; ++i) {
        uint64_t value = data[i];
        uint64_t best_delta = 0;
        int64_t best_delta_index = -1;
        uint64_t best_compressed_value = value;
        uint64_t compressed_value;

        for (size_t j = 0; j < global_base_set->count; ++j) {
            uint64_t base = global_base_set->bases[j];
            if (value >= base) {
                compressed_value = value - base;
                if (compressed_value < best_compressed_value) {
                    best_compressed_value = compressed_value;
                    best_delta = base;
                    best_delta_index = j;
                }
            }
        }

        if (best_delta_index >= 0) {
            write_variable_length_integer(best_delta_index + 1, &output_buffer);
        } else {
            write_variable_length_integer(0, &output_buffer);
        }

        write_variable_length_integer(best_compressed_value, &output_buffer);
    }

    *output_size = output_buffer - buffer_start;
}


void extract_cache_lines(Segment *segments, size_t num_segments, uint64_t **cache_lines, size_t *num_cache_lines) {
    size_t total_cache_lines = 0;

    
    for (size_t i = 0; i < num_segments; ++i) {
        total_cache_lines += (segments[i].size + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE;
    }

    *num_cache_lines = total_cache_lines;
    *cache_lines = malloc(total_cache_lines * sizeof(uint64_t));

    uint64_t *cache_line_ptr = *cache_lines;
    for (size_t i = 0; i < num_segments; ++i) {
        size_t remaining_size = segments[i].size;
        uint8_t *segment_data_ptr = segments[i].data;

        while (remaining_size > 0) {
            size_t copy_size = (remaining_size >= CACHE_LINE_SIZE) ? CACHE_LINE_SIZE : remaining_size;
            memcpy(cache_line_ptr, segment_data_ptr, copy_size);

            if (copy_size < CACHE_LINE_SIZE) {
                memset(cache_line_ptr + copy_size, 0, CACHE_LINE_SIZE - copy_size);
            }

            cache_line_ptr += CACHE_LINE_SIZE / sizeof(uint64_t);
            segment_data_ptr += copy_size;
            remaining_size -= copy_size;
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    
    char *filename = argv[1];
    FILE *file = fopen(filename, "rb");


    if (!file) {
        printf("Error opening file %s\n", filename);
        return 1;
    }

Elf64_Ehdr elf_header;


fread(&elf_header, sizeof(Elf64_Ehdr), 1, file);

if (memcmp(elf_header.e_ident, ELFMAG, SELFMAG) != 0) {
    printf("Error: not an ELF file\n");
    fclose(file);
    return 1;
}

if (elf_header.e_ident[EI_CLASS] != ELFCLASS64) {
    printf("Error: not a 64-bit ELF file\n");
    fclose(file);
    return 1;
}


fseek(file, elf_header.e_phoff, SEEK_SET);
Elf64_Phdr *program_headers = malloc(elf_header.e_phnum * sizeof(Elf64_Phdr));
fread(program_headers, sizeof(Elf64_Phdr), elf_header.e_phnum, file);


Segment *segments = malloc(elf_header.e_phnum * sizeof(Segment));
size_t segment_count = 0;

for (int i = 0; i < elf_header.e_phnum; ++i) {
    if (program_headers[i].p_type == PT_LOAD) {
        segments[segment_count].address = program_headers[i].p_vaddr;
        segments[segment_count].size = program_headers[i].p_filesz;
        segments[segment_count].data = malloc(program_headers[i].p_filesz);

        fseek(file, program_headers[i].p_offset, SEEK_SET);
        fread(segments[segment_count].data, 1, program_headers[i].p_filesz, file);

        segment_count++;
    }
}

fclose(file);
free(program_headers);


uint64_t *cache_lines;
size_t num_cache_lines;
extract_cache_lines(segments, segment_count, &cache_lines, &num_cache_lines);


GlobalBaseSet global_base_set;
establish_global_base_set_hb(cache_lines, num_cache_lines, &global_base_set, MAX_BASES);


uint8_t *compressed_data = malloc(num_cache_lines * sizeof(uint64_t));
size_t compressed_data_size;
gbdi_compress(cache_lines, num_cache_lines, &global_base_set, compressed_data, &compressed_data_size);


printf("Original size: %zu Compressed size: %zu Compression ratio: %.2f\n",num_cache_lines * sizeof(uint64_t) , compressed_data_size, (double)(num_cache_lines * sizeof(uint64_t)) / compressed_data_size);



free(segments);
free(cache_lines);
free(global_base_set.bases);
free(compressed_data);

return 0;
}
