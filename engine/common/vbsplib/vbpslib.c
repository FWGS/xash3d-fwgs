//
// Created by RED on 23.01.2024.
//

#include "vbpslib.h"
#include "common.h"

#define CHECK_AND_COPY_LUMP(dst, dst_count, lump, data, type, mod) \
    do{                                                            \
        assert((lump)->decompressed_size==0);                      \
        const byte* lump_data = (data)+(lump)->offset;                   \
        (dst) = Mem_Calloc((mod)->mempool,(lump)->size);           \
        memcpy((dst), lump_data, (lump)->size);                    \
        (dst_count) = (lump)->size/sizeof(type);                 \
    }while(false)
void VBSPLib_loadBSP(vbsp_t *vbsp, const byte *data, model_t *mod)
{
    memcpy(&vbsp->header,data,sizeof(vbsp_header_t));

    CHECK_AND_COPY_LUMP(vbsp->entities,vbsp->entities_size, &vbsp->header.lumps[VBSP_LUMP_ENTITIES], data, char, mod);

}
