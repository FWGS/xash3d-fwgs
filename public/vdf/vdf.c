#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "vdf.h"
#include "../../filesystem/fscallback.h"

#define CHAR_SPACE ' '
#define CHAR_TAB '\t'
#define CHAR_NEWLINE '\n'
#define CHAR_DOUBLE_QUOTE '"'
#define CHAR_OPEN_CURLY_BRACKET '{'
#define CHAR_CLOSED_CURLY_BRACKET '}'
#define CHAR_OPEN_ANGLED_BRACKET '['
#define CHAR_CLOSED_ANGLED_BRACKET ']'
#define CHAR_FRONTSLASH '/'
#define CHAR_BACKSLASH '\\'

#define FMT_UNKNOWN_CHAR "Encountered Unknown Character %c (%li)\n"

static char* local_strndup_escape(const char* s, size_t n)
{
    if (!s)
       return NULL;

    char* retval = malloc(n + 1);
    strncpy(retval, s, n);
    retval[n] = '\0';

    char* head = retval;
    char* tail = retval + n;

    while (*head)
    {
        if (*head == CHAR_BACKSLASH)
        {
            switch (head[1])
            {
                case 'n':
                    memmove(head, head+1, (size_t)(tail-head));
                    *head = CHAR_NEWLINE;
                    break;

                case 't':
                    memmove(head, head+1, (size_t)(tail-head));
                    *head = CHAR_TAB;
                    break;

                case CHAR_BACKSLASH:
                case CHAR_DOUBLE_QUOTE:
                    memmove(head, head+1, (size_t)(tail-head));
                    break;
            }
        }
        ++head;
    }

    return retval;
}

static void print_escaped(const char* s)
{
    while (*s)
    {
        switch(*s)
        {
            case CHAR_DOUBLE_QUOTE:
                printf("\\\"");
                break;

            case CHAR_TAB:
                printf("\\t");
                break;

            case CHAR_NEWLINE:
                printf("\\n");
                break;

            case CHAR_BACKSLASH:
                printf("\\\\");
                break;

            default:
                printf("%c", *s);
                break;
        }

        ++s;
    }
}

struct vdf_object* vdf_parse_buffer(const char* buffer, size_t size)
{
    if (!buffer)
        return NULL;

    struct vdf_object* root_object = malloc(sizeof(struct vdf_object));
    root_object->key = NULL;
    root_object->parent = NULL;
    root_object->type = VDF_TYPE_NONE;
    root_object->conditional = NULL;

    struct vdf_object* o = root_object;

    const char* head = buffer;
    const char* tail = head;

    const char* end = buffer + size;

    const char* buf = NULL;

    while (end > tail)
    {
        switch (*tail)
        {
            case CHAR_DOUBLE_QUOTE:
                if (tail > buffer && *(tail-1) == CHAR_BACKSLASH)
                    break;

                if (!buf)
                {
                    buf = tail+1;
                }
                else if (o->key)
                {
                    size_t len = tail - buf;
                    size_t digits = 0;
                    size_t chars = 0;

                    for (size_t i = 0; i < len; ++i)
                    {
                        if (isdigit(buf[i]))
                            digits++;

                        if (isalpha(buf[i]))
                            chars++;
                    }

                    if (len && digits == len)
                    {
                        o->type = VDF_TYPE_INT;
                    }
                    else
                    {
                        o->type = VDF_TYPE_STRING;
                    }

                    switch (o->type)
                    {
                        case VDF_TYPE_INT:
                            o->data.data_int = strtol(buf, NULL, 10);
                            break;

                        case VDF_TYPE_STRING:
                            o->data.data_string.len = len;
                            o->data.data_string.str = local_strndup_escape(buf, len);
                            break;

                        default:
                            assert(0);
                            break;
                    }

                    buf = NULL;

                    if (o->parent && o->parent->type == VDF_TYPE_ARRAY)
                    {
                        o = o->parent;
                        assert(o->type == VDF_TYPE_ARRAY);

                        o->data.data_array.len++;
                        o->data.data_array.data_value = realloc(o->data.data_array.data_value, (sizeof(void*)) * (o->data.data_array.len + 1));
                        o->data.data_array.data_value[o->data.data_array.len] = malloc(sizeof(struct vdf_object)),
                        o->data.data_array.data_value[o->data.data_array.len]->parent = o;

                        o = o->data.data_array.data_value[o->data.data_array.len];
                        o->key = NULL;
                        o->type = VDF_TYPE_NONE;
                        o->conditional = NULL;
                    }
                }
                else
                {
                    size_t len = tail - buf;
                    o->key = local_strndup_escape(buf, len);
                    buf = NULL;
                }
                break;

            case CHAR_OPEN_CURLY_BRACKET:
                assert(!buf);
                assert(o->type == VDF_TYPE_NONE);

                if (o->parent && o->parent->type == VDF_TYPE_ARRAY)
                    o->parent->data.data_array.len++;

                o->type = VDF_TYPE_ARRAY;
                o->data.data_array.len = 0;
                o->data.data_array.data_value = malloc((sizeof(void*)) * (o->data.data_array.len + 1));
                o->data.data_array.data_value[o->data.data_array.len] = malloc(sizeof(struct vdf_object));
                o->data.data_array.data_value[o->data.data_array.len]->parent = o;

                o = o->data.data_array.data_value[o->data.data_array.len];
                o->key = NULL;
                o->type = VDF_TYPE_NONE;
                o->conditional = NULL;
                break;

            case CHAR_CLOSED_CURLY_BRACKET:
                assert(!buf);


                o = o->parent;
                assert(o);
                if (o->parent)
                {
                    o = o->parent;
                    assert(o->type == VDF_TYPE_ARRAY);

                    o->data.data_array.data_value = realloc(o->data.data_array.data_value, (sizeof(void*)) * (o->data.data_array.len + 1));
                    o->data.data_array.data_value[o->data.data_array.len] = malloc(sizeof(struct vdf_object)),
                    o->data.data_array.data_value[o->data.data_array.len]->parent = o;

                    o = o->data.data_array.data_value[o->data.data_array.len];
                    o->key = NULL;
                    o->type = VDF_TYPE_NONE;
                    o->conditional = NULL;
                }
				else
				{
					root_object->type = VDF_TYPE_ARRAY;
					return root_object;
				}

                break;

            case CHAR_FRONTSLASH:
                if (!buf)
                    while (*tail != '\0' && *tail != CHAR_NEWLINE)
                        ++tail;

                break;

            case CHAR_OPEN_ANGLED_BRACKET:
                if (!buf)
                {
                    struct vdf_object* prev = o->parent->data.data_array.data_value[o->parent->data.data_array.len-1];
                    assert(!prev->conditional);

                    buf = tail+1;

                    while (*tail != '\0' && *tail != CHAR_CLOSED_ANGLED_BRACKET)
                        ++tail;

                    prev->conditional = local_strndup_escape(buf, tail-buf);

                    buf = NULL;
                }

                break;

            default:
                if (!buf)
                {
                    // we found something we are probably not suppose to
                    // the easiest way out is to just terminate
                    vdf_free_object(root_object);
                    return NULL;
                }
                break;

            case CHAR_NEWLINE:
            case CHAR_SPACE:
            case CHAR_TAB:
                break;
        }
        ++tail;
    }
    return root_object;
}

struct vdf_object* vdf_parse_file(const char* path)
{
    struct vdf_object* o = NULL;
    if (!path)
        return o;

	
    file_t* fd = FS_Open(path, "r", true);
    if (!fd)
        return o;

    FS_Seek(fd, 0L, SEEK_END);
    size_t file_size = FS_Tell(fd);
    FS_Seek(fd, 0L, SEEK_SET);

    if (file_size)
    {
        char* buffer = malloc(file_size);
        FS_Read(fd, buffer, file_size);

        o = vdf_parse_buffer(buffer, file_size);
        free(buffer);
    }

    FS_Close(fd);

    return o;
}


size_t vdf_object_get_array_length(const struct vdf_object* o)
{
    assert(o);
    assert(o->type == VDF_TYPE_ARRAY);

    return o->data.data_array.len;
}

struct vdf_object* vdf_object_index_array(const struct vdf_object* o, const size_t index)
{
    assert(o);
    assert(o->type == VDF_TYPE_ARRAY);
    assert(o->data.data_array.len > index);

    return o->data.data_array.data_value[index];
}

struct vdf_object* vdf_object_index_array_str(const struct vdf_object* o, const char* str)
{
    if (!o || !str || o->type != VDF_TYPE_ARRAY)
        return NULL;

    for (size_t i = 0; i < o->data.data_array.len; ++i)
    {
        struct vdf_object* k = o->data.data_array.data_value[i];
        if (!strcmp(k->key, str))
            return k;
    }
    return NULL;
}

const char* vdf_object_get_string(const struct vdf_object* o)
{
    assert(o->type == VDF_TYPE_STRING);

    return o->data.data_string.str;
}

int vdf_object_get_int(const struct vdf_object* o)
{
    assert(o->type == VDF_TYPE_INT);

    return o->data.data_int;
}

static void vdf_print_object_indent(const struct vdf_object* o, const int l)
{
    if (!o)
        return;

    char* spacing = "\t";

    for (int k = 0; k < l; ++k)
        printf("%s", spacing);

    printf("\"");
    print_escaped(o->key);
    printf("\"");

    switch (o->type)
    {
        case VDF_TYPE_ARRAY:
            printf("\n");
            for (int k = 0; k < l; ++k)
                printf("%s", spacing);
            printf("{\n");
            for (size_t i = 0; i < o->data.data_array.len; ++i)
                vdf_print_object_indent(o->data.data_array.data_value[i], l+1);

            for (int k = 0; k < l; ++k)
                printf("%s", spacing);
            printf("}");
            break;

        case VDF_TYPE_INT:
            printf("\t\t\"%lli\"\n", o->data.data_int);
            break;

        case VDF_TYPE_STRING:
            printf("\t\t\"");
            print_escaped(o->data.data_string.str);
            printf("\"");
            break;

        default:
        case VDF_TYPE_NONE:
            assert(0);
            break;
    }

    if (o->conditional)
        printf("\t\t[%s]", o->conditional);

    printf("\n");
}

void vdf_print_object(struct vdf_object* o)
{
    vdf_print_object_indent(o, 0);
}

void vdf_free_object(struct vdf_object* o)
{
    if (!o)
        return;

    switch (o->type)
    {
        case VDF_TYPE_ARRAY:
            for (size_t i = 0; i <= o->data.data_array.len; ++i)
            {
                vdf_free_object(o->data.data_array.data_value[i]);
            }
            free(o->data.data_array.data_value);
            break;


        case VDF_TYPE_STRING:
            if (o->data.data_string.str)
                free(o->data.data_string.str);
            break;

        default:
        case VDF_TYPE_NONE:
            break;

    }

    if (o->key)
        free(o->key);

    if (o->conditional)
        free(o->conditional);
    free(o);
}
