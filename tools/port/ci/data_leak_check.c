/* Disc-window audit. Only paths, offsets, counts and rolling hashes leave this process. */
#define _XOPEN_SOURCE 700
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define WINDOW 64
#define COVER_WINDOW 16
#define NONE UINT32_MAX

typedef struct { const char *path; unsigned char *data, *covered; size_t size, leaks, first_leak_offset; uint64_t first_leak_hash; } File;
typedef struct { uint64_t hash; uint32_t file, offset, next, nontrivial; } Entry;
static File *files;
static Entry *entries;
static uint32_t *buckets;
static size_t count, capacity, bucket_count;


static void fail(const char *path) {
    fprintf(stderr, "%s: input/resource error (errno=%d)\n", path, errno);
    exit(2);
}
static void *allocate(size_t n, size_t size) {
    void *p = calloc(n ? n : 1, size);
    if (!p) fail("allocation");
    return p;
}
static File map_file(const char *path) {
    File f = { .path = path };
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0) fail(path);
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size > SIZE_MAX) fail(path);
    f.size = (size_t)st.st_size;
    if (f.size) {
        f.data = mmap(NULL, f.size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (f.data == MAP_FAILED) fail(path);
    }
    close(fd);
    return f;
}
static void unmap_file(File *f) { if (f->size) munmap(f->data, f->size); }
static size_t bucket(uint64_t hash) {
    hash ^= hash >> 33; hash *= UINT64_C(0xff51afd7ed558ccd); hash ^= hash >> 33;
    return (size_t)hash & (bucket_count - 1);
}
static void add(uint64_t hash, uint32_t file, size_t offset, unsigned distinct) {
    if (count >= UINT32_MAX || offset > UINT32_MAX) fail(files[file].path);
    if (count == capacity) {
        size_t next = capacity ? capacity * 2 : 4096;
        if (next < capacity || next > SIZE_MAX / sizeof(*entries)) fail("window table");
        Entry *p = realloc(entries, next * sizeof(*entries));
        if (!p) fail("window table");
        entries = p; capacity = next;
    }
    entries[count++] = (Entry){hash, file, (uint32_t)offset, NONE, distinct >= 5};
}
/* Hash every sliding window; distinct counts are updated in constant time.
 * ponytail: fewer than five byte values is the approved trivial-padding heuristic. */
typedef void (*Visit)(uint64_t, const unsigned char *, size_t, unsigned, void *);
static void windows(File *f, size_t width, Visit visit, void *arg) {
    uint64_t power = 1;
    for (size_t i = 1; i < width; ++i) power *= 257;
    unsigned frequencies[256] = {0}, distinct = 0;
    uint64_t hash = 0;
    if (f->size < width) return;
    for (size_t i = 0; i < width; ++i) {
        unsigned b = f->data[i];
        if (!frequencies[b]++) ++distinct;
        hash = hash * 257 + b;
    }
    for (size_t offset = 0;; ++offset) {
        visit(hash, f->data + offset, offset, distinct, arg);
        if (offset == f->size - width) break;
        unsigned out = f->data[offset], in = f->data[offset + width];
        hash = (hash - out * power) * 257 + in;
        if (!--frequencies[out]) --distinct;
        if (!frequencies[in]++) ++distinct;
    }
}
static void index_window(uint64_t hash, const unsigned char *p, size_t off, unsigned distinct, void *arg) {
    (void)p;
    add(hash, *(uint32_t *)arg, off, distinct);
}
static void cover_window(uint64_t hash, const unsigned char *p, size_t off, unsigned distinct, void *arg) {
    (void)off; (void)arg; (void)distinct;
    uint32_t *link = &buckets[bucket(hash)];
    while (*link != NONE) {
        Entry *e = &entries[*link]; File *f = &files[e->file];
        if (e->hash == hash && !memcmp(f->data + e->offset, p, COVER_WINDOW)) {
            memset(f->covered + e->offset, 1, COVER_WINDOW);
            *link = e->next; /* A matching window only needs to mark coverage once. */
        } else link = &e->next;
    }
}
static void link_entries(void) {
    memset(buckets, 255, bucket_count * sizeof(*buckets));
    for (size_t i = 0; i < count; ++i) {
        size_t b = bucket(entries[i].hash);
        entries[i].next = buckets[b]; buckets[b] = (uint32_t)i;
    }
}
typedef struct { size_t matches, reports; } Scan;
static void disc_window(uint64_t hash, const unsigned char *p, size_t off, unsigned distinct, void *arg) {
    Scan *scan = arg;
    if (distinct < 5) return;
    size_t b = bucket(hash);
    uint32_t *link = &buckets[b];
    while (*link != NONE) {
        Entry *e = &entries[*link]; File *f = &files[e->file];
        if (e->hash == hash && !memcmp(f->data + e->offset, p, WINDOW)) {
            if (scan->reports++ < 20)
                printf("leak: %s offset=%" PRIu32 " disc_offset=%zu hash=%016" PRIx64 "\n",
                       f->path, e->offset, off, hash);
            ++scan->matches;
            if (!f->leaks++) { f->first_leak_offset = e->offset; f->first_leak_hash = hash; }
            *link = e->next; /* Count/report each artifact window only once. */
        } else link = &e->next;
    }
}
/* wasm-ld's Off column precedes Emscripten's final rewriting. Match passive
 * segments in order and require exact sizes; translate input ranges relative to
 * their segment, never by a global file-offset delta. Unsupported maps fail shut. */
typedef struct { size_t offset, size; } Segment;
static uint32_t leb(File *f, size_t *pos, size_t end) {
    uint32_t value = 0;
    for (unsigned shift = 0; shift < 35; shift += 7) {
        if (*pos >= end) fail(f->path);
        unsigned byte = f->data[(*pos)++];
        if (shift == 28 && byte > 15) fail(f->path);
        value |= (uint32_t)(byte & 127) << shift;
        if (!(byte & 128)) return value;
    }
    fail(f->path); return 0;
}
static void map_error(const char *path) {
    fprintf(stderr, "%s: unusable wasm linker map/data attribution\n", path);
    exit(2);
}
static void map_coverage(File *f, const char *path, char **exceptions, int nexceptions) {
    if (f->size < 8 || memcmp(f->data, "\0asm\1\0\0\0", 8)) map_error(path);
    Segment *segments = NULL;
    size_t nsegments = 0, data_begin = 0, data_end = 0;
    for (size_t pos = 8; pos < f->size;) {
        unsigned id = f->data[pos++];
        size_t size = leb(f, &pos, f->size);
        if (size > f->size - pos) map_error(path);
        size_t end = pos + size;
        if (id == 11) {
            if (data_end) map_error(path);
            data_begin = pos; data_end = end;
            nsegments = leb(f, &pos, end);
            if (!nsegments || nsegments > size / 2) map_error(path);
            segments = allocate(nsegments, sizeof(*segments));
            for (size_t i = 0; i < nsegments; ++i) {
                if (leb(f, &pos, end) != 1) map_error(path); /* passive segments */
                size_t length = leb(f, &pos, end);
                if (length > end - pos) map_error(path);
                segments[i] = (Segment){pos, length};
                pos += length;
            }
            if (pos != end) map_error(path);
        }
        pos = end;
    }
    if (!data_end) map_error(path);
    FILE *map = fopen(path, "r");
    if (!map) fail(path);
    char canonical[PATH_MAX];
    if (!realpath(path, canonical)) fail(path);
    char *slash = strrchr(canonical, '/');
    if (!slash) map_error(path);
    *slash = '\0';
    char **allowed = allocate((size_t)nexceptions, sizeof(*allowed));
    for (int i = 0; i < nexceptions; ++i) {
        allowed[i] = realpath(exceptions[i], NULL);
        if (!allowed[i]) fail(exceptions[i]);
    }
    /* Only zero padding is covered without an owner. Object rows override this
     * even if the 16-byte exceptions would cover those bytes. */
    for (size_t i = data_begin; i < data_end; ++i) f->covered[i] = f->data[i] == 0;
    char *line = NULL; size_t line_capacity = 0;
    size_t segment = 0, input_end = 0, map_payload = 0, object_bytes = 0, game_bytes = 0;
    int in_data = 0, saw_data = 0, in_segment = 0;
    while (getline(&line, &line_capacity, map) >= 0) {
        char addr[32], off[32], length[32]; int column = 0;
        if (sscanf(line, "%31s %31s %31s %n", addr, off, length, &column) != 3 || !column)
            continue; /* column headings */
        char *name = line + column;
        name[strcspn(name, "\r\n")] = '\0';
        if (column == 27 && !strcmp(name, "DATA")) {
            if (saw_data++) map_error(path);
            in_data = 1; continue;
        }
        if (!in_data) continue;
        if (column == 27 && (!strcmp(name, ".bss") || !strncmp(name, "CUSTOM(", 7))) {
            in_data = 0; continue;
        }
        char *end;
        unsigned long long offset = strtoull(off, &end, 16);
        if (*end || offset > SIZE_MAX) map_error(path);
        unsigned long long size = strtoull(length, &end, 16);
        if (*end || size > SIZE_MAX) map_error(path);
        if (column == 27) {
            if (segment >= nsegments || size != segments[segment].size) map_error(path);
            size_t header = 1;
            for (size_t n = (size_t)size;; n >>= 7) { ++header; if (n < 128) break; }
            if (offset > SIZE_MAX - header) map_error(path);
            map_payload = (size_t)offset + header;
            input_end = 0; ++segment; in_segment = 1;
        } else if (column == 35) {
            if (!in_segment || offset < map_payload) map_error(path);
            Segment seg = segments[segment - 1];
            size_t relative = (size_t)offset - map_payload;
            if (relative < input_end || relative > seg.size || size > seg.size - relative)
                map_error(path);
            input_end = relative + (size_t)size;
            char *separator = strstr(name, ":(");
            if (!separator) map_error(path);
            *separator = '\0';
            int game = 0;
            char object[PATH_MAX], resolved[PATH_MAX];
            int written = name[0] == '/' ? snprintf(object, sizeof(object), "%s", name) :
                snprintf(object, sizeof(object), "%s/%s", canonical, name);
            if (written < 0 || (size_t)written >= sizeof(object)) map_error(path);
            if (realpath(object, resolved))
                for (int i = 0; i < nexceptions; ++i)
                    if (!strcmp(resolved, allowed[i])) { game = 1; break; }
            memset(f->covered + seg.offset + relative, game, (size_t)size);
            object_bytes += (size_t)size;
            if (game) game_bytes += (size_t)size;
        } else if (column != 43) map_error(path);
    }
    if (ferror(map)) fail(path);
    fclose(map); free(line);
    if (!saw_data || segment != nsegments || !object_bytes) map_error(path);
    printf("map=%s segments=%zu attributed_bytes=%zu game_bytes=%zu\n",
           path, nsegments, object_bytes, game_bytes);
    for (int i = 0; i < nexceptions; ++i) free(allowed[i]);
    free(allowed); free(segments);
}
int main(int argc, char **argv) {
    const char *disc = NULL, *linker_map = NULL;
    /* Optional attribution applies to the single wasm artifact only. */
    if (argc > 3 && !strcmp(argv[1], "--wasm-map")) {
        linker_map = argv[2]; argv += 2; argc -= 2;
    }
    int exception_begin = 0, exception_end = 0, artifact_begin = 0;
    if (argc >= 6 && !strcmp(argv[1], "--disc") && !strcmp(argv[3], "--exception")) {
        disc = argv[2]; exception_begin = 4;
        for (int i = 4; i < argc; ++i) if (!strcmp(argv[i], "--")) {
            exception_end = i; artifact_begin = i + 1; break;
        }
    }
    if (!disc || !artifact_begin || artifact_begin >= argc ||
        (linker_map && argc - artifact_begin != 1)) {
        fprintf(stderr, "usage: dw1-leak-check [--wasm-map <map>] --disc <pack> --exception <files...> -- <artifacts...>\n");
        return 2;
    }
    size_t nfiles = (size_t)(argc - artifact_begin), total = 0, analyzed = 0, covered = 0;
    files = allocate(nfiles, sizeof(*files));
    for (size_t i = 0; i < nfiles; ++i) {
        files[i] = map_file(argv[artifact_begin + i]);
        files[i].covered = allocate(files[i].size, 1);
        if (files[i].size > SIZE_MAX - total) fail("artifact size");
        total += files[i].size;
        uint32_t id = (uint32_t)i;
        if (exception_begin != exception_end) windows(&files[i], COVER_WINDOW, index_window, &id);
    }
    bucket_count = 1024;
    while (bucket_count < total) {
        if (bucket_count > SIZE_MAX / 2 / sizeof(*buckets)) fail("hash buckets");
        bucket_count *= 2;
    }
    buckets = allocate(bucket_count, sizeof(*buckets));
    link_entries();
    for (int i = exception_begin; i < exception_end; ++i) {
        File exception = map_file(argv[i]);
        windows(&exception, COVER_WINDOW, cover_window, NULL);
        unmap_file(&exception);
    }
    if (linker_map)
        map_coverage(&files[0], linker_map, argv + exception_begin, exception_end - exception_begin);
    count = 0;
    for (size_t i = 0; i < nfiles; ++i) {
        uint32_t id = (uint32_t)i;
        windows(&files[i], WINDOW, index_window, &id);
    }
    for (size_t i = 0; i < count; ++i) analyzed += entries[i].nontrivial;
    /* Sliding uncovered-byte count: coverage can come from different exception windows. */
    size_t retained = 0, entry = 0;
    for (size_t i = 0; i < nfiles; ++i) {
        File *f = &files[i];
        unsigned missing = 0;
        for (size_t j = 0; j < WINDOW && j < f->size; ++j) missing += !f->covered[j];
        for (size_t off = 0; f->size >= WINDOW && off <= f->size - WINDOW; ++off) {
            if (entry < count && entries[entry].file == i && entries[entry].offset == off) {
                if (entries[entry].nontrivial) {
                    if (missing) entries[retained++] = entries[entry]; else ++covered;
                }
                ++entry;
            }
            if (off < f->size - WINDOW) {
                missing -= !f->covered[off]; missing += !f->covered[off + WINDOW];
            }
        }
        free(f->covered);
    }
    count = retained;
    link_entries();
    File pack = map_file(disc);
    if (pack.size < WINDOW) {
        fprintf(stderr, "%s: Pack shorter than %d bytes\n", disc, WINDOW);
        return 2;
    }
    Scan scan = {0};
    windows(&pack, WINDOW, disc_window, &scan);
    printf("artifacts=%zu bytes=%zu nontrivial_windows=%zu covered_windows=%zu checked_windows=%zu leak_windows=%zu\n",
           nfiles, total, analyzed, covered, count, scan.matches);
    for (size_t i = 0; i < nfiles; ++i)
        if (files[i].leaks)
            printf("artifact=%s offset=%zu hash=%016" PRIx64 " leak_windows=%zu\n",
                   files[i].path, files[i].first_leak_offset, files[i].first_leak_hash, files[i].leaks);
    unmap_file(&pack);
    for (size_t i = 0; i < nfiles; ++i) unmap_file(&files[i]);
    free(files); free(entries); free(buckets);
    return scan.matches ? 1 : 0;
}
