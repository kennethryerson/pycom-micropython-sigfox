/*
 * This file is derived from the MicroPython project, http://micropython.org/
 *
 * Copyright (c) 2016, Pycom Limited and its licensors.
 *
 * This software is licensed under the GNU GPL version 3 or any later version,
 * with permitted additional terms. For more information see the Pycom Licence
 * v1.0 document supplied with this file, or available at:
 * https://www.pycom.io/opensource/licensing
 */

/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 * Copyright (c) 2015 Daniel Campora
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>
#include <string.h>

#include "py/mpstate.h"
#include "py/nlr.h"
#include "py/objtuple.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include "lib/oofatfs/ff.h"
#include "lib/oofatfs/diskio.h"
#include "genhdr/mpversion.h"
#include "moduos.h"
#include "sflash_diskio.h"
#include "extmod/vfs.h"
#include "extmod/vfs_fat.h"
#include "random.h"
#include "pycom_version.h"
#include "timeutils.h"
#include "machuart.h"
#include "pybsd.h"
#include "mpexception.h"

/// \module os - basic "operating system" services
///
/// The `os` module contains functions for filesystem access and `urandom`.
///
/// The filesystem has `/` as the root directory, and the available physical
/// drives are accessible from here.  They are currently:
///
///     /flash      -- the serial flash filesystem
///
/// On boot up, the current directory is `/flash`.

/******************************************************************************
 DEFINE PUBLIC FUNCTIONS
 ******************************************************************************/

void osmount_unmount_all (void) {
    //TODO
    /*
    for (mp_uint_t i = 0; i < MP_STATE_PORT(mount_obj_list).len; i++) {
        os_fs_mount_t *mount_obj = ((os_fs_mount_t *)(MP_STATE_PORT(mount_obj_list).items[i]));
        unmount(mount_obj);
    }
    */
}

/******************************************************************************/
// Micro Python bindings
//

STATIC const qstr os_uname_info_fields[] = {
    MP_QSTR_sysname, MP_QSTR_nodename,
    MP_QSTR_release, MP_QSTR_version
    ,MP_QSTR_machine
#if defined(LOPY) || defined(LOPY4) || defined(FIPY)
    ,MP_QSTR_lorawan
#endif
#if defined(SIPY) || defined(FIPY) || defined(LOPY4)
    ,MP_QSTR_sigfox
#endif
};
STATIC const MP_DEFINE_STR_OBJ(os_uname_info_sysname_obj, MICROPY_PY_SYS_PLATFORM);
STATIC const MP_DEFINE_STR_OBJ(os_uname_info_nodename_obj, MICROPY_PY_SYS_PLATFORM);
STATIC const MP_DEFINE_STR_OBJ(os_uname_info_release_obj, SW_VERSION_NUMBER);
STATIC const MP_DEFINE_STR_OBJ(os_uname_info_version_obj, MICROPY_GIT_TAG " on " MICROPY_BUILD_DATE);
STATIC const MP_DEFINE_STR_OBJ(os_uname_info_machine_obj, MICROPY_HW_BOARD_NAME " with " MICROPY_HW_MCU_NAME);
#if defined(LOPY) || defined(LOPY4) || defined(FIPY)
STATIC const MP_DEFINE_STR_OBJ(os_uname_info_lorawan_obj, LORAWAN_VERSION_NUMBER);
#endif
#if defined(SIPY) || defined (LOPY4) || defined (FIPY)
STATIC const MP_DEFINE_STR_OBJ(os_uname_info_sigfox_obj, SIGFOX_VERSION_NUMBER);
#endif
STATIC MP_DEFINE_ATTRTUPLE(
    os_uname_info_obj
    ,os_uname_info_fields
#if defined(FIPY) || defined (LOPY4)
    ,7
#else
#if defined(LOPY) || defined(SIPY)
    ,6
    #else
    ,5
    #endif
#endif
    ,(mp_obj_t)&os_uname_info_sysname_obj
    ,(mp_obj_t)&os_uname_info_nodename_obj
    ,(mp_obj_t)&os_uname_info_release_obj
    ,(mp_obj_t)&os_uname_info_version_obj
    ,(mp_obj_t)&os_uname_info_machine_obj
#if defined(LOPY) || defined(LOPY4) || defined(FIPY)
    ,(mp_obj_t)&os_uname_info_lorawan_obj
#endif
#if defined(SIPY) || defined (LOPY4) || defined(FIPY)
    ,(mp_obj_t)&os_uname_info_sigfox_obj
#endif
);

STATIC mp_obj_t os_uname(void) {
    return (mp_obj_t)&os_uname_info_obj;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(os_uname_obj, os_uname);

STATIC mp_obj_t os_getfree(mp_obj_t path_in) {
    const char *path = mp_obj_str_get_str(path_in);
    fs_user_mount_t *vfs_fat = NULL;
    DWORD nclst;

    // free space on flash
    for (mp_vfs_mount_t *vfs = MP_STATE_VM(vfs_mount_table); vfs != NULL; vfs = vfs->next) {
        if (strncmp(path, vfs->str, vfs->len) == 0) {
            // assumes that it's a FatFs filesystem
            vfs_fat = MP_OBJ_TO_PTR(vfs->obj);
            break;
        }
    }

    if (vfs_fat == NULL)
    {
        mp_raise_OSError(MP_ENOENT);
    }

    FRESULT res = f_getfree(&vfs_fat->fatfs, &nclst);;
    if (FR_OK != res) {
        mp_raise_OSError(fresult_to_errno_table[res]);
    }

    uint64_t free_space = ((uint64_t)vfs_fat->fatfs.csize * nclst)
#if _MAX_SS != _MIN_SS
    * vfs_fat->fatfs.ssize;
#else
    * _MIN_SS;
#endif

    return mp_obj_new_int_from_ull(free_space / 1024);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(os_getfree_obj, os_getfree);

STATIC mp_obj_t os_sync(void) {
    sflash_disk_flush();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(os_sync_obj, os_sync);

STATIC mp_obj_t os_urandom(mp_obj_t num) {
    mp_int_t n = mp_obj_get_int(num);
    vstr_t vstr;
    vstr_init_len(&vstr, n);
    for (int i = 0; i < n; i++) {
        vstr.buf[i] = rng_get();
    }
    return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(os_urandom_obj, os_urandom);

STATIC mp_obj_t os_dupterm(uint n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        if (MP_STATE_PORT(mp_os_stream_o) == MP_OBJ_NULL) {
            return mp_const_none;
        } else {
            return MP_STATE_PORT(mp_os_stream_o);
        }
    } else {
        mp_obj_t stream_o = args[0];
        if (stream_o == mp_const_none) {
            MP_STATE_PORT(mp_os_stream_o) = MP_OBJ_NULL;
        } else {
            if (!MP_OBJ_IS_TYPE(stream_o, &mach_uart_type)) {
                // must be a stream-like object providing at least read and write methods
                mp_load_method(stream_o, MP_QSTR_read, MP_STATE_PORT(mp_os_read));
                mp_load_method(stream_o, MP_QSTR_write, MP_STATE_PORT(mp_os_write));
            }
            MP_STATE_PORT(mp_os_stream_o) = stream_o;
        }
        return mp_const_none;
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(os_dupterm_obj, 0, 1, os_dupterm);

STATIC const mp_rom_map_elem_t os_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),        MP_ROM_QSTR(MP_QSTR_uos) },

    { MP_ROM_QSTR(MP_QSTR_uname),           MP_ROM_PTR(&os_uname_obj) },
    { MP_ROM_QSTR(MP_QSTR_chdir),           MP_ROM_PTR(&mp_vfs_chdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_getcwd),          MP_ROM_PTR(&mp_vfs_getcwd_obj) },
    { MP_ROM_QSTR(MP_QSTR_ilistdir),        MP_ROM_PTR(&mp_vfs_ilistdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_listdir),         MP_ROM_PTR(&mp_vfs_listdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkdir),           MP_ROM_PTR(&mp_vfs_mkdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_rename),          MP_ROM_PTR(&mp_vfs_rename_obj)},
    { MP_ROM_QSTR(MP_QSTR_remove),          MP_ROM_PTR(&mp_vfs_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_rmdir),           MP_ROM_PTR(&mp_vfs_rmdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_stat),            MP_ROM_PTR(&mp_vfs_stat_obj) },
    { MP_ROM_QSTR(MP_QSTR_statvfs),         MP_ROM_PTR(&mp_vfs_statvfs_obj) },
    { MP_ROM_QSTR(MP_QSTR_unlink),          MP_ROM_PTR(&mp_vfs_remove_obj) },     // unlink aliases to remove
    { MP_ROM_QSTR(MP_QSTR_sync),            MP_ROM_PTR(&os_sync_obj) },
    { MP_ROM_QSTR(MP_QSTR_urandom),         MP_ROM_PTR(&os_urandom_obj) },
    { MP_ROM_QSTR(MP_QSTR_getfree),         MP_ROM_PTR(&os_getfree_obj) },

    // MicroPython additions
    { MP_ROM_QSTR(MP_QSTR_mount),           MP_ROM_PTR(&mp_vfs_mount_obj) },
    { MP_ROM_QSTR(MP_QSTR_umount),          MP_ROM_PTR(&mp_vfs_umount_obj) },
    { MP_ROM_QSTR(MP_QSTR_VfsFat),          MP_ROM_PTR(&mp_fat_vfs_type) },
    { MP_ROM_QSTR(MP_QSTR_dupterm),         MP_ROM_PTR(&os_dupterm_obj) },

    /// \constant sep - separation character used in paths
    { MP_ROM_QSTR(MP_QSTR_sep),             MP_ROM_QSTR(MP_QSTR__slash_) },
};

STATIC MP_DEFINE_CONST_DICT(os_module_globals, os_module_globals_table);

const mp_obj_module_t mp_module_uos = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&os_module_globals,
};
