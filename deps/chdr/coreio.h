#pragma once
#include <stdio.h>

#define core_file FILE

#define core_fopen(file) fopen(file, "rb")
#define core_fseek fseek
#define core_fread(fc, buff, len) fread(buff, 1, len, fc)
#define core_fclose fclose
#define core_ftell ftell


static size_t core_fsize(core_file* f)
{
    size_t p=ftell(f);
    fseek(f,0,SEEK_END);
    size_t rv=ftell(f);
    fseek(f,p,SEEK_SET);
    return rv;
}
