// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

#include <py/builtin.h>
#include <sys/stat.h>

mp_import_stat_t
mp_import_stat( const char *path )
{
    struct stat st;
    if ( stat( path, &st ) == 0 )
    {
        if ( ( st.st_mode & S_IFMT ) == S_IFDIR )
        {
            return MP_IMPORT_STAT_DIR;
        }
        if ( ( st.st_mode & S_IFMT ) == S_IFREG )
        {
            return MP_IMPORT_STAT_FILE;
        }
    }
    return MP_IMPORT_STAT_NO_EXIST;
}

// Currently no filesystem access is allowed from within scripts:
mp_obj_t
mp_builtin_open( size_t n_args, const mp_obj_t *args, mp_map_t *kwargs )
{
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW( mp_builtin_open_obj, 1, mp_builtin_open );
