#ifndef LIB_IRODS_SMB_HPP
#define LIB_IRODS_SMB_HPP

#include <irods/rodsClient.h>

typedef int error_code;

struct irods_context_t;
typedef struct irods_context_t irods_context;

typedef int irods_object_type;
#define IOT_DATA_OBJECT 1
#define IOT_COLLECTION  2

typedef struct _irods_stat_info
{
    long long size;
    irods_object_type type;
    int mode;
    long id;
    char owner_name[128];
    char owner_zone[128];
    long long creation_time;
    long long modified_time;
} irods_stat_info;

typedef struct _irods_char_array
{
    char* data;
    long length;
} irods_char_array;

typedef struct _irods_string_array
{
    irods_char_array* strings;
    long size;
} irods_string_array;

typedef struct _irods_fd
{
    char path[1024];
    long inode;
} irods_fd;

typedef struct _irods_collection_entry
{
    char path[1024];
    long inode;
} irods_collection_entry;

typedef int irods_directory_stream;

#ifdef __cplusplus
extern "C" {
#endif

error_code ismb_test();

int ismb_init();

irods_context* ismb_create_context(const char* _smb_path);

void ismb_destroy_context(irods_context* _ctx);

error_code ismb_connect(irods_context* _ctx);

error_code ismb_disconnect(irods_context* _ctx);

void ismb_map(irods_context* _ctx, const char* _path);

error_code ismb_get_fd_by_path(irods_context* _ctx, const char* _path, irods_fd* _fd);

error_code ismb_stat_path(irods_context* _ctx, const char* _path, irods_stat_info* _stat_info);

void ismb_list(irods_context* _ctx, const char* _path, irods_string_array* _entries);

void ismb_free_string_array(irods_string_array* _string_array);

void ismb_free_string(const char* _string);

error_code ismb_change_directory(irods_context* _ctx, const char* _target_dir);

//
// Directory Operations
//

error_code ismb_get_working_directory(irods_context* _ctx, char** _dir);

error_code ismb_opendir(irods_context* _ctx, const char* _path, irods_directory_stream* _dir_stream);

error_code ismb_fdopendir(irods_context* _ctx, const char* _path, irods_directory_stream* _dir_stream);

error_code ismb_readdir(irods_context* _ctx,
                        irods_directory_stream _dir_stream,
                        irods_collection_entry* _coll_entry);

error_code ismb_seekdir(irods_context* _ctx, const char* _path);

error_code ismb_telldir(irods_context* _ctx, const char* _path);

error_code ismb_rewind_dir(irods_context* _ctx, const char* _path);

error_code ismb_mkdir(irods_context* _ctx, const char* _path);

error_code ismb_rmdir(irods_context* _ctx, const char* _path);

void ismb_closedir(irods_context* _ctx, irods_directory_stream _dir_stream);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // LIB_IRODS_SMB_HPP

