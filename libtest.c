#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libirods_smb.h"

int main(int _argc, char* _argv[])
{
    error_code ec = ismb_test();
    printf("ismb_test :: error code = %i\n", ec);

    irods_context* ctx = ismb_create_context("./irods");

    if (!ctx)
    {
        printf("malloc failed.\n");
        return 1;
    }

    ec = ismb_connect(ctx);
    printf("ismb_connect :: error code = %i\n", ec);

    if (_argc > 1)
    {
        irods_stat_info stats;
        ec = ismb_stat(ctx, _argv[1], &stats);
        printf("ismb_stat :: error code = %i\n", ec);

        /*
        irods_string_array entries;
        ismb_list(ctx, _argv[1], &entries);

        if (entries.size > 0)
        {
            printf("\n");

            for (long i = 0; i < entries.size; ++i)
                printf("ismb_list :: entry %ld = %s\n", i, entries.strings[i].data);

            ismb_free_string_array(&entries);
        }
        */

        irods_collection_stream* coll_stream;
        if (ismb_opendir(ctx, _argv[1], &coll_stream) == 0)
        {
            printf("listing collection entries ...\n");

            while (1)
            {
                struct dirent* entry = ismb_readdir(ctx, coll_stream);

                if (!entry)
                    break;
                    
                printf("ismb_readdir :: entry = %ld, %s\n", entry->d_ino, entry->d_name);
            }

            ismb_closedir(ctx, coll_stream);
        }

        if (_argc > 1)
        {
            printf("removed directory [%s] = %i\n", _argv[2], ismb_rmdir(ctx, _argv[2]));
        }

        if (_argc > 2)
        {
            int fd = ismb_open(ctx, _argv[3], O_CREAT | O_TRUNC | O_RDWR, 0777);
            printf("opened file descriptor = %i\n", fd);

            if (fd >= 0)
            {
                const char buf[] = "IT WORKS!!!\n";
                printf("wrote %d bytes to file.\n", ismb_write(ctx, fd, (void*) buf, sizeof(buf)));

                irods_stat_info stats;
                printf("ismb_fstat :: error code = %i\n", ismb_fstat(ctx, fd, &stats));

                printf("closed file descriptor = %s\n", (ismb_close(ctx, fd) == 0 ? "true" : "false"));
            }
        }
    }
    else
    {
        printf("ismb_stat_path :: skipped.  usage: %s <irods_path>\n", _argv[0]);
    }

    ec = ismb_disconnect(ctx);
    printf("ismb_disconnect :: error code = %i\n", ec);

    ismb_destroy_context(ctx);

    return 0;
}
