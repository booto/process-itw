// gcc process-itw.c -o process-itw -std=c99 -lgd
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <gd.h>


#define ITW_MAGIC (0x4954575fU)


typedef struct {
  uint8_t is_leaf;
  uint8_t value;
  uint32_t id;
  int32_t left;
  int32_t right;
  int32_t parent;
  float weight;
} huffman_node;


void print_node(huffman_node *h, size_t index, size_t indent) {
  for(size_t j=0; j<indent; ++j) putchar(' ');
  printf("-id: %d, weight: %f", index, h[index].weight);
  if (h[index].is_leaf) {
    printf(" value: %02hhx\n", h[index].value);
  } else {
    printf("\n");
    print_node(h, h[index].left, indent+2);
    print_node(h, h[index].right, indent+2);
  }
}

int get_num_nodes_with_no_parent(huffman_node *h, size_t n) {
  int total = 0;
  for (size_t i = 0; i < n; ++i) {
    if (h[i].parent == -1) total++;
  }
  return total;
}

size_t get_min_weight_node_with_no_parent(huffman_node *h, size_t n) {
  float min = 100.0f;
  size_t min_index = n;
  for (size_t i = 0; i < n; ++i) {
    if (h[i].parent == -1) {
      if (h[i].weight < min) {
        min_index = i;
        min = h[i].weight;
      }
    }
  }
  return min_index;
}


float uint32_to_f32(uint32_t u) {
  union {
    uint32_t u;
    float f;
  } conv;
  conv.u = u;
  return conv.f;
}

uint32_t get_beuint16(uint8_t *buffer) {
  return buffer[1] | (buffer[0] << 8);
}


uint32_t get_beuint32(uint8_t *buffer) {
  return get_beuint16(buffer + 2) | (get_beuint16(buffer) << 16);
}

uint32_t get_leuint16(uint8_t *buffer) {
  return buffer[0] | (buffer[1] << 8);
}


uint32_t get_leuint32(uint8_t *buffer) {
  return get_leuint16(buffer) | (get_leuint16(buffer + 2) << 16);
}

uint32_t fget_leuint16(FILE *f) {
  uint32_t rv = fgetc(f);
  rv = (fgetc(f) << 8) | rv;
  return rv;
}

uint32_t fget_leuint32(FILE *f) {
  uint32_t rv = fget_leuint16(f);
  rv = (fget_leuint16(f) << 16) | rv;
  return rv;
}


uint32_t fget_beuint16(FILE *f) {
  uint32_t rv = fgetc(f);
  rv = (rv << 8) | fgetc(f);
  return rv;
}

uint32_t fget_beuint32(FILE *f) {
  uint32_t rv = fget_beuint16(f);
  rv = (rv << 16) | fget_beuint16(f);
  return rv;
}


int huffman_decode(uint8_t *input, size_t input_length, uint8_t **output, size_t *out_length) {
  uint8_t *curr_pos = input;

  if ((output == NULL) || (input == NULL) ||
      (out_length == NULL)) {
    return -1;
  }


  if (input_length < sizeof(uint32_t)) { // num_huffman_leaves
    fprintf(stderr, "error: compressed data short (num_huffman_leaves)\n");
    return -1;
  }
  size_t num_huffman_leaves = get_leuint32(curr_pos);
  curr_pos += 4;

// printf("num_huffman_leaves: %zu\n", num_huffman_leaves);


// never going to be more nodes than this
  huffman_node *nodes = calloc(num_huffman_leaves*2, sizeof(huffman_node));
  if (nodes == NULL) {
    fprintf(stderr, "error: couldn't allocate %zu huffman nodes\n", num_huffman_leaves*2);
    return -1;
  }

  size_t num_nodes = 0;
  if (input_length < sizeof(uint32_t) +  // num huffman leaves
                     (num_huffman_leaves * (sizeof(uint32_t) + sizeof(float)))) {  // huffman leaves
    fprintf(stderr, "error: compressed data short (nodes)\n");
    free(nodes);
    return -1;
  }

  for (size_t i = 0; i < num_huffman_leaves; ++i) {
    nodes[num_nodes].value = (uint8_t) get_leuint32(curr_pos);
    curr_pos += 4;
    nodes[num_nodes].weight = uint32_to_f32(get_leuint32(curr_pos));
    curr_pos += 4;
    nodes[num_nodes].id = i;
    nodes[num_nodes].parent = -1;
    nodes[num_nodes].left = -1;
    nodes[num_nodes].right = -1;
    nodes[num_nodes].is_leaf = 1;
    num_nodes++;
  }

  while (get_num_nodes_with_no_parent(nodes, num_nodes) > 1) {
    nodes[num_nodes].id = num_nodes;
    nodes[num_nodes].parent = -1;

    size_t left = get_min_weight_node_with_no_parent(nodes, num_nodes);
    assert(left < num_nodes);
    nodes[left].parent = num_nodes;
    nodes[num_nodes].left = left;

    size_t right = get_min_weight_node_with_no_parent(nodes, num_nodes);
    assert(right < num_nodes);
    nodes[right].parent = num_nodes;
    nodes[num_nodes].right = right;

    nodes[num_nodes].weight =  nodes[left].weight + nodes[right].weight;
    num_nodes++;
  }

  size_t root = get_min_weight_node_with_no_parent(nodes, num_nodes);

//  print_node(nodes, root, 0);


  size_t out_buffer_capacity = 0x4000;
  uint8_t *out_buffer = malloc(out_buffer_capacity);
  size_t out_buffer_used = 0;

  if (out_buffer == NULL) {
    fprintf(stderr, "error: couldn't allocate %zu bytes output buffer\n", out_buffer_capacity);
    free(nodes);
    return -1;
  }
  

  if (input_length < sizeof(uint32_t) +  // num huffman leaves
                     (num_huffman_leaves * (sizeof(uint32_t) + sizeof(float))) +  // huffman leaves
                     sizeof(uint32_t)) {  // bits_to_process
    fprintf(stderr, "error: compressed data short (bits_to_process)\n");
    free(out_buffer);
    free(nodes);
    return -1;
  }
  uint32_t bits_to_process = get_leuint32(curr_pos);
  curr_pos += 4;
  //printf("bits_to_process: %08x\n", bits_to_process);

  if (input_length < sizeof(uint32_t) +                                           // num huffman leaves
                     (num_huffman_leaves * (sizeof(uint32_t) + sizeof(float))) +  // huffman leaves
                     sizeof(uint32_t) +                                           // bits_to_process
                     ((bits_to_process + 7)>>3)) {                                  // bytes payload
    fprintf(stderr, "error: compressed data short (bytes_payload)\n");
    free(out_buffer);
    free(nodes);
    return -1;
  }

  size_t bits_in_current_byte = 0;
  size_t bits_processed = 0;
  uint8_t current_byte;

  size_t current_node = root;

  while (bits_processed < bits_to_process) {
    if (bits_in_current_byte == 0) {
      current_byte = *curr_pos;
      curr_pos++;
      bits_in_current_byte = 8;
    }

    if ((current_byte & 1) == 0) {
      current_node = nodes[current_node].left;
    } else {
      current_node = nodes[current_node].right;
    }
    bits_processed++;
    bits_in_current_byte--;
    current_byte >>= 1;

    if (nodes[current_node].is_leaf) {
      out_buffer[out_buffer_used] = nodes[current_node].value;
      out_buffer_used++;
      if (out_buffer_used == out_buffer_capacity) {
        out_buffer_capacity *= 2;
        uint8_t *new_buffer = realloc(out_buffer, out_buffer_capacity);
        if (new_buffer == NULL) {
          fprintf(stderr, "error: couldn't reallocate %zu bytes output buffer\n", out_buffer_capacity);
          free(out_buffer);
          free(nodes);
          return -1;
        }
        out_buffer = new_buffer;
      }

      current_node = root;
    }
  }
  free(nodes);

  if (current_node != root) {
    fprintf(stderr, "warning: processing did not end on a leaf node\n");
  }

  *output = out_buffer;
  *out_length = out_buffer_used;

  return 0;
}

int process_itw(const char *input_filename) {
  FILE *input = fopen(input_filename, "rb");
  if (input == NULL) {
    fprintf(stderr, "error: couldn't open file: %s\n", strerror(errno));
    return -1;
  }

  uint32_t magic = fget_beuint32(input);
  if (magic != ITW_MAGIC) {
    fclose(input);
    fprintf(stderr, "error: incorrect magic: expected %08x, got %08x\n", ITW_MAGIC, magic);
    return -1;
  }

  fget_beuint16(input);  // don't care about this
  uint16_t width = fget_beuint16(input);
  uint16_t height = fget_beuint16(input);
  fget_beuint16(input);  // not used
  uint16_t type = fget_beuint16(input);

  if (type != 0x400) {
    fprintf(stderr, "error: unknown type: expected %04hx, got %04hx\n", 0x400, type);
    fclose(input);
    return -1;
  }

  ssize_t read_len;

  uint32_t num_palette_entries = fgetc(input);
  uint8_t *palette = malloc(num_palette_entries);
  if (palette == NULL) {
    fprintf(stderr, "error: couldn't malloc palette (%u)\n", num_palette_entries);
    fclose(input);
    return -1;
  }

  read_len = fread(palette, sizeof(uint8_t), num_palette_entries, input);
  if (read_len < 0) {
    fprintf(stderr, "error: problem reading palette: %s\n", strerror(errno));
    free(palette);
    fclose(input);
    return -1;
  }
  if (read_len != num_palette_entries) {
    fprintf(stderr, "error: problem reading palette: short read (wanted %u, got %zd)\n", num_palette_entries, read_len);
    free(palette);
    fclose(input);
    return -1;
  }

  uint32_t pixel_data_compressed_size = fget_beuint32(input);
  uint8_t *pixel_data_compressed = malloc(pixel_data_compressed_size);
  if (pixel_data_compressed == NULL) {
    fprintf(stderr, "error: couldn't malloc pixel_data_compressed (%u)\n", pixel_data_compressed_size);
    free(palette);
    fclose(input);
    return -1;
  }
  read_len = fread(pixel_data_compressed, sizeof(uint8_t), pixel_data_compressed_size, input);
  if (read_len < 0) {
    fprintf(stderr, "error: problem reading compressed pixel data: %s\n", strerror(errno));
    free(pixel_data_compressed);
    free(palette);
    fclose(input);
    return -1;
  }
  if (read_len != pixel_data_compressed_size) {
    fprintf(stderr, "error: problem reading compressed pixel data: short read (wanted %u, got %zd)\n", pixel_data_compressed_size, read_len);
    free(pixel_data_compressed);
    free(palette);
    fclose(input);
    return -1;
  }

  uint32_t repeat_data_compressed_size = fget_beuint32(input);
  uint8_t *repeat_data_compressed = malloc(repeat_data_compressed_size * sizeof(uint8_t));
  if (repeat_data_compressed == NULL) {
    fprintf(stderr, "error: couldn't malloc pixel_data_compressed (%u)\n", pixel_data_compressed_size);
    free(palette);
    fclose(input);
    return -1;
  }
  read_len = fread(repeat_data_compressed, sizeof(uint8_t), repeat_data_compressed_size, input);
  if (read_len < 0) {
    fprintf(stderr, "error: problem reading compressed repeat data: %s\n", strerror(errno));
    free(repeat_data_compressed);
    free(pixel_data_compressed);
    free(palette);
    fclose(input);
    return -1;
  }
  if (read_len != repeat_data_compressed_size) {
    fprintf(stderr, "error: problem reading compressed repeat data: short read (wanted %zu, got %zd)\n", pixel_data_compressed_size, read_len);
    free(repeat_data_compressed);
    free(pixel_data_compressed);
    free(palette);
    fclose(input);
    return -1;
  }

  fclose(input);

  int rv;

  uint8_t *pixel_data;
  size_t pixel_data_len;

  uint8_t *repeat_data;
  size_t repeat_data_len;

  rv = huffman_decode(pixel_data_compressed, pixel_data_compressed_size, &pixel_data, &pixel_data_len);
  if (rv != 0) {
    fprintf(stderr, "error: couldn't decompress pixel data\n");
    free(repeat_data_compressed);
    free(pixel_data_compressed);
    free(palette);
    return -1;
  }

  free(pixel_data_compressed);

  rv = huffman_decode(repeat_data_compressed, repeat_data_compressed_size, &repeat_data, &repeat_data_len);
  if (rv != 0) {
    fprintf(stderr, "error: couldn't decompress repeat data\n");
    free(pixel_data);
    free(repeat_data_compressed);
    free(palette);
    return -1;
  }

  free(repeat_data_compressed);


  // sanity check
  size_t num_repeats = 0;
  for (size_t i = 0; i<pixel_data_len; ++i) {
    if (pixel_data[i] < (8 + num_palette_entries)) {
      num_repeats++;
    }
  }

  if (num_repeats != repeat_data_len) {
    fprintf(stderr, "error: repeat data does not match pixel data\n");
    free(pixel_data);
    free(repeat_data);
    free(palette);
    return -1;
  }

  size_t pixel_data_index = 0;
  size_t repeat_data_index = 0;
  size_t i = 0;
  uint8_t col;

  size_t output_filename_len = strlen(input_filename) + 5;
  char *output_filename = malloc(output_filename_len);

  if (output_filename == NULL) {
    fprintf(stderr, "error: couldn't malloc output filename buffer\n");
    free(pixel_data);
    free(repeat_data);
    free(palette);
    return -1;
  }

  if (snprintf(output_filename, output_filename_len, "%s.png", input_filename) < 0) {
    fprintf(stderr, "error: couldn't snprintf output filename buffer\n");
    free(output_filename);
    free(pixel_data);
    free(repeat_data);
    free(palette);
    return -1;
  }

  gdImagePtr img = gdImageCreateTrueColor(width, height);
  if (img == NULL) {
    fprintf(stderr, "error: couldn't open output file('%s'): %s\n", output_filename, strerror(errno));
    free(output_filename);
    free(pixel_data);
    free(repeat_data);
    free(palette);
    return -1;
    
  }
  while (pixel_data_index < pixel_data_len) {
    if (pixel_data[pixel_data_index] < 8 + num_palette_entries) {
      size_t repeat = 1<<repeat_data[repeat_data_index];
      col = palette[pixel_data[pixel_data_index] - 8];
      while (repeat--) {
        gdImageSetPixel(img, i%width, i/width, gdTrueColor(col, col, col));
        i++;
      }
      repeat_data_index++;
      pixel_data_index++;
    } else {
      col = palette[pixel_data[pixel_data_index] - 8 - num_palette_entries];
      gdImageSetPixel(img, i%width, i/width, gdTrueColor(col, col, col));
      i++;
      pixel_data_index++;
    }
  }

  free(repeat_data);
  free(pixel_data);
  free(palette);

  
  FILE *out = fopen(output_filename, "wb");
  if (out == NULL) {
    fprintf(stderr, "error: couldn't open output file('%s'): %s\n", output_filename, strerror(errno));
    free(output_filename);
    gdImageDestroy(img);
    return -1;
  }

  gdImagePng(img, out); 
  fclose(out);

  free(output_filename);
  gdImageDestroy(img);

  return 0;
}


int main(int argc, char *argv[]) {
  for (int i=1; i<argc; ++i) {
    if (process_itw(argv[i]) != 0) {
      fprintf(stderr, "error: couldn't process '%s'\n", argv[i]);
    }
  }
}
