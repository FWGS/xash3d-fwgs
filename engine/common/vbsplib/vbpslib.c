//
// Created by RED on 23.01.2024.
//

#include "vbpslib.h"
#include "common.h"

#define CHECK_AND_COPY(dst, dst_count, lump, data, type, mod) \
    do{                                                       \
        assert((lump)->decompressed_size==0);                 \
        const byte* lump_data = (data)+(lump)->offset;        \
        (dst) = Mem_Calloc((mod)->mempool,(lump)->size);      \
        memcpy((dst), lump_data, (lump)->size);               \
        (dst_count) = (lump)->size/sizeof(type);              \
    }while(false)
void VBSPLib_loadBSP(vbsp_t *vbsp, const byte *data, model_t *mod)
{
    memcpy(&vbsp->header,data,sizeof(vbsp_header_t));

    CHECK_AND_COPY(vbsp->entities,vbsp->entities_size, &vbsp->header.lumps[VBSP_LUMP_ENTITIES], data, char, mod);
    CHECK_AND_COPY(vbsp->planes,vbsp->plane_count, &vbsp->header.lumps[VBSP_LUMP_PLANES], data, vbsp_dplane_t, mod);
    CHECK_AND_COPY(vbsp->texture_data,vbsp->texture_data_count, &vbsp->header.lumps[VBSP_LUMP_TEXDATA], data, vbsp_dtexdata_t, mod);
    CHECK_AND_COPY(vbsp->vertices,vbsp->vertex_count, &vbsp->header.lumps[VBSP_LUMP_VERTEXES], data, vbsp_vertex_t, mod);
    CHECK_AND_COPY(vbsp->nodes,vbsp->node_count, &vbsp->header.lumps[VBSP_LUMP_NODES], data, vbsp_dnode_t, mod);
    CHECK_AND_COPY(vbsp->texture_info,vbsp->texture_info_count, &vbsp->header.lumps[VBSP_LUMP_TEXINFO], data, vbsp_texinfo_t, mod);
    CHECK_AND_COPY(vbsp->faces,vbsp->face_count, &vbsp->header.lumps[VBSP_LUMP_FACES], data, vbsp_dface_t, mod);
    CHECK_AND_COPY(vbsp->orig_faces,vbsp->orig_face_count, &vbsp->header.lumps[VBSP_LUMP_ORIGINALFACES], data, vbsp_dface_t, mod);
    CHECK_AND_COPY(vbsp->leafs,vbsp->leaf_count, &vbsp->header.lumps[VBSP_LUMP_LEAFS], data, vbsp_dleaf_t, mod);
    CHECK_AND_COPY(vbsp->edges,vbsp->edge_count, &vbsp->header.lumps[VBSP_LUMP_EDGES], data, vbsp_dedge_t, mod);
    CHECK_AND_COPY(vbsp->surf_edges,vbsp->surf_edge_count, &vbsp->header.lumps[VBSP_LUMP_SURFEDGES], data, vbsp_surfedge_t, mod);
    CHECK_AND_COPY(vbsp->models,vbsp->model_count, &vbsp->header.lumps[VBSP_LUMP_MODELS], data, vbsp_dmodel_t, mod);
    CHECK_AND_COPY(vbsp->leaf2face_map,vbsp->leaf2face_map_size, &vbsp->header.lumps[VBSP_LUMP_LEAFFACES], data, vbsp_leaf_face_t, mod);
    CHECK_AND_COPY(vbsp->leaf2brush_map,vbsp->leaf2brush_map_size, &vbsp->header.lumps[VBSP_LUMP_LEAFBRUSHES], data, vbsp_leaf_brush_t, mod);
    CHECK_AND_COPY(vbsp->brushes,vbsp->brush_count, &vbsp->header.lumps[VBSP_LUMP_BRUSHES], data, vbsp_dbrush_t, mod);
    CHECK_AND_COPY(vbsp->brush_sides,vbsp->brush_sides_count, &vbsp->header.lumps[VBSP_LUMP_BRUSHSIDES], data, vbsp_dbrushside_t, mod);
    CHECK_AND_COPY(vbsp->tex_string_data,vbsp->tex_string_data_size, &vbsp->header.lumps[VBSP_LUMP_TEXDATA_STRING_DATA], data, vbsp_texdata_stringdata_t, mod);
    CHECK_AND_COPY(vbsp->tex_string_table,vbsp->tex_string_table_size, &vbsp->header.lumps[VBSP_LUMP_TEXDATA_STRING_TABLE], data, vbsp_texdata_stringtable_t , mod);
    CHECK_AND_COPY(vbsp->vis_info,vbsp->vis_info_count, &vbsp->header.lumps[VBSP_LUMP_VISIBILITY], data, vbsp_dvis_t , mod);
    return;
}
