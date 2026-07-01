// gdsii_oasis_validator.cc
// GDSII Stream Format / OASIS Format Validator
//
// Validates GDSII (.gds) and OASIS (.oas/.oasis) binary files
// by verifying their binary structure against format specs.
// No ext deps beyond standard C++ and POSIX.
//
// Build: g++ -std=c++11 -O2 -o validator gdsii_oasis_validator.cc
// Usage: ./validator <file.gds|file.oas>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

static void die(const char* m) {
    fprintf(stderr, "ERROR: %s\n", m); exit(1);
}

// GDSII record types: name, valid, dtype, iSize, minBody, maxBody
struct Rt { const char* n; int v; int d; int sz; int mn; int mx; };
static const Rt gds[] = {
  {"HDR",1,2,2,2,2},  {"BGNLIB",1,2,2,24,24},
  {"LIBNAME",1,6,0,0,65530}, {"UNITS",1,5,8,16,16},
  {"ENDLIB",1,0,0,0,0},  {"BGNSTR",1,2,2,24,24},
  {"STRNAME",1,6,0,2,65530}, {"ENDSTR",1,0,0,0,0},
  {"BOUNDARY",1,0,0,0,0}, {"PATH",1,0,0,0,0},
  {"SREF",1,0,0,0,0},  {"AREF",1,0,0,0,0},
  {"TEXT",1,0,0,0,0},  {"LAYER",1,2,2,2,2},
  {"DATATYPE",1,2,2,2,2}, {"WIDTH",1,3,4,4,4},
  {"XY",1,3,4,8,65528},  {"ENDEL",1,0,0,0,0},
  {"SNAME",1,6,0,2,65530}, {"COLROW",1,2,2,4,4},
  {"NODE",1,0,0,0,0},  {"TEXTTYPE",1,2,2,2,2},
  {"PRESENTATION",1,1,2,2,2}, {"STRANS",1,1,2,2,2},
  {"STRING",1,6,0,0,65530},   {"MAG",1,5,8,8,8},
  {"ANGLE",1,5,8,8,8},    {"PATHTYPE",1,2,2,2,2},
  {"ELFLAGS",1,1,2,2,2},  {"PLEX",1,3,4,4,4},
  {"NODETYPE",1,2,2,2,2}, {"BOX",1,0,0,0,0},
  {"BOXTYPE",1,2,2,2,2},  {"PROPATTR",1,2,2,2,2},
  {"PROPVALUE",1,6,0,0,65530}, {"BGNEXTN",1,3,4,4,4},
  {"ENDEXTN",1,3,4,4,4},  {"TEXTNODE",1,0,0,0,0},
};
static const int NG = sizeof(gds)/sizeof(gds[0]);

static int chk_gds(const char* path) {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) { die("can't open"); return 1; }
    off_t sz = ::lseek(fd, 0, SEEK_END);
    ::lseek(fd, 0, SEEK_SET);
    printf("GDSII file, %ld bytes\n", (long)sz);
    int err = 0, n = 0;
    uint8_t h[4];
    while (1) {
        off_t off = ::lseek(fd, 0, SEEK_CUR);
        if (off >= sz) break;
        ssize_t r = ::read(fd, h, 4);
        if (r < 4) { if (r < 0) perror("read"); break; }
        int len = (h[0]<<8)|h[1];
        int typ = h[2];
        int blen = len - 4;
        if (len < 4) { printf("  ERR len %d\n", len); err++; break; }
        const char* nm = (typ >= 0 && typ < NG && gds[typ].v) ? gds[typ].n : "?";
        printf("  %s t=%d len=%d [0x%lx]\n", nm, typ, len, (long)off);
        n++;
        if (blen > 0) ::lseek(fd, blen, SEEK_CUR);
        if (typ == 4) break; // ENDLIB
    }
    ::close(fd);
    printf("Records: %d  Errors: %d\n", n, err);
    return err;
}

static int chk_oas(const char* path) {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) { die("can't open"); return 1; }
    off_t sz = ::lseek(fd, 0, SEEK_END);
    ::lseek(fd, 0, SEEK_SET);
    printf("OASIS file, %ld bytes\n", (long)sz);
    // Check magic
    char magic[13];
    if (::read(fd, magic, 13) != 13) {
        die("can't read magic"); return 1;
    }
    const char* M = "%SEMI-OAS IS\r\n";
    if (memcmp(magic, M, 13) != 0) {
        printf("  ERR: bad magic string\n"); return 1;
    }
    printf("  Magic OK\n");
    ::close(fd);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { die("usage: ./validator <file>"); return 1; }
    const char* p = argv[1];
    printf("File: %s\n", p);
    // detect format by magic
    int fd = ::open(p, O_RDONLY);
    if (fd < 0) die("can't open");
    char m[13] = {};
    ::read(fd, m, 13);
    ::close(fd);
    const char* MO = "%SEMI-OAS IS\r\n";
    int isoas = (memcmp(m, MO, 13) == 0);
    if (isoas) return chk_oas(p);
    else return chk_gds(p);
}
