/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George on behalf of Pycom Ltd
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

#include <stdio.h>
#include <string.h>

#include "py/runtime.h"
#include "py/stackctrl.h"

#if MICROPY_PY_THREAD

#include "py/mpthread.h"

#if 0 // print debugging info
#define DEBUG_PRINT (1)
#define DEBUG_printf DEBUG_printf
#else // don't print debugging info
#define DEBUG_PRINT (0)
#define DEBUG_printf(...) (void)0
#endif

/****************************************************************/
// _thread module

STATIC mp_obj_t mod_thread_get_ident(void) {
    return mp_obj_new_int_from_uint((uintptr_t)mp_thread_get_state());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mod_thread_get_ident_obj, mod_thread_get_ident);

typedef struct _thread_entry_args_t {
    mp_obj_t fun;
    size_t n_args;
    size_t n_kw;
    const mp_obj_t *args;
} thread_entry_args_t;

STATIC void *thread_entry(void *args_in) {
    thread_entry_args_t *args = (thread_entry_args_t*)args_in;

    mp_state_thread_t ts;
    mp_thread_set_state(&ts);

    mp_stack_set_top(&ts + 1); // need to include ts in root-pointer scan
    mp_stack_set_limit(16 * 1024); // fixed stack limit for now

    // TODO set more thread-specific state here:
    //  mp_pending_exception? (root pointer)
    //  cur_exception (root pointer)
    //  dict_locals? (root pointer) uPy doesn't make a new locals dict for functions, just for classes, so it's different to CPy

    DEBUG_printf("[thread] start ts=%p args=%p stack=%p\n", &ts, &args, MP_STATE_THREAD(stack_top));

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_call_function_n_kw(args->fun, args->n_args, args->n_kw, args->args);
        nlr_pop();
    } else {
        // uncaught exception
        // check for SystemExit
        if (mp_obj_is_subclass_fast(mp_obj_get_type((mp_obj_t)nlr.ret_val), &mp_type_SystemExit)) {
            // swallow exception silently
        } else {
            // print exception out
            mp_printf(&mp_plat_print, "Unhandled exception in thread started by ");
            mp_obj_print_helper(&mp_plat_print, args->fun, PRINT_REPR);
            mp_printf(&mp_plat_print, "\n");
            mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
        }
    }

    DEBUG_printf("[thread] finish ts=%p\n", &ts);

    return NULL;
}

STATIC mp_obj_t mod_thread_start_new_thread(size_t n_args, const mp_obj_t *args) {
    mp_uint_t pos_args_len;
    mp_obj_t *pos_args_items;
    mp_obj_get_array(args[1], &pos_args_len, &pos_args_items);
    thread_entry_args_t *th_args = m_new_obj(thread_entry_args_t);
    th_args->fun = args[0];
    if (n_args == 2) {
        // just position arguments
        th_args->n_args = pos_args_len;
        th_args->n_kw = 0;
        th_args->args = pos_args_items;
    } else {
        // positional and keyword arguments
        if (mp_obj_get_type(args[2]) != &mp_type_dict) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_TypeError, "expecting a dict for keyword args"));
        }
        mp_map_t *map = &((mp_obj_dict_t*)MP_OBJ_TO_PTR(args[2]))->map;
        th_args->n_args = pos_args_len;
        th_args->n_kw = map->used;
        mp_obj_t *all_args = m_new(mp_obj_t, th_args->n_args + 2 * th_args->n_kw);
        memcpy(all_args, pos_args_items, pos_args_len * sizeof(mp_obj_t));
        for (size_t i = 0, n = pos_args_len; i < map->alloc; ++i) {
            if (MP_MAP_SLOT_IS_FILLED(map, i)) {
                all_args[n++] = map->table[i].key;
                all_args[n++] = map->table[i].value;
            }
        }
        th_args->args = all_args;
    }
    // TODO implement setting thread stack size
    mp_thread_create(thread_entry, th_args);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_thread_start_new_thread_obj, 2, 3, mod_thread_start_new_thread);

STATIC const mp_rom_map_elem_t mp_module_thread_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__thread) },
    { MP_ROM_QSTR(MP_QSTR_get_ident), MP_ROM_PTR(&mod_thread_get_ident_obj) },
    { MP_ROM_QSTR(MP_QSTR_start_new_thread), MP_ROM_PTR(&mod_thread_start_new_thread_obj) },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_thread_globals, mp_module_thread_globals_table);

const mp_obj_module_t mp_module_thread = {
    .base = { &mp_type_module },
    .name = MP_QSTR__thread,
    .globals = (mp_obj_dict_t*)&mp_module_thread_globals,
};

#endif // MICROPY_PY_THREAD