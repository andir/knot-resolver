/*  Copyright (C) 2015 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <uv.h>

#include "daemon/engine.h"
#include "daemon/bindings.h"
#include "daemon/ffimodule.h"
#include "lib/module.h"
#include "lib/layer.h"

#if LUA_VERSION_NUM >= 502
#define l_resume(L, argc) lua_resume((L), NULL, (argc))
#else
#define l_resume(L, argc) lua_resume((L), (argc))
#endif

/** @internal Helper for retrieving the right function entrypoint. */
static inline lua_State *l_ffi_preface(struct kr_module *module, const char *call) {
	lua_State *L = module->lib;
	lua_rawgeti(L, LUA_REGISTRYINDEX, (intptr_t)module->data);
	lua_getfield(L, -1, call);
	lua_remove(L, -2);
	lua_pushlightuserdata(L, module);
	return L;
}

/** @internal Continue with coroutine. */
static void l_ffi_resume_cb(uv_idle_t *check)
{
	lua_State *L = check->data;
	int status = l_resume(L, 0);
	if (status != LUA_YIELD) {
		uv_idle_stop(check); /* Stop coroutine */
		uv_close((uv_handle_t *)check, (uv_close_cb)free);
	}
	lua_pop(L, lua_gettop(L));
}

/** @internal Schedule deferred continuation. */
static int l_ffi_defer(lua_State *L)
{
	uv_idle_t *check = malloc(sizeof(*check));
	if (!check) {
		return kr_error(ENOMEM);
	}
	uv_idle_init(uv_default_loop(), check);
	check->data = L;
	return uv_idle_start(check, l_ffi_resume_cb);
}

/** @internal Helper for calling the entrypoint. */
static inline int l_ffi_call(lua_State *L, int argc)
{
	int status = lua_pcall(L, argc, LUA_MULTRET, 0);
	if (status != 0) {
		lua_pop(L, 1);
		return kr_error(EIO);
	}

	int n = lua_gettop(L);
	if (n > 0) {
		if (lua_isthread(L, -1)) { /* Continuations */
			status = l_ffi_defer(lua_tothread(L, -1));
		} else if (lua_isnumber(L, -1)) { /* Return code */
			status = lua_tonumber(L, 1);
		}
		lua_pop(L, n);
	}
	return status;
}

static int l_ffi_init(struct kr_module *module)
{
	lua_State *L = l_ffi_preface(module, "init");
	return l_ffi_call(L, 1);
}

static int l_ffi_deinit(struct kr_module *module)
{
	lua_State *L = l_ffi_preface(module, "deinit");
	int ret = l_ffi_call(L, 1);
	/* Free the layer API wrapper */
	lua_rawgeti(L, LUA_REGISTRYINDEX, (intptr_t)module->data);
	lua_getfield(L, -1, "_layercdata");
	free(lua_touserdata(L, -1));
	lua_pop(L, 2);
	/* Unref module and unset 'lib', so the module
	 * interface doesn't attempt to close it.
	 */
	lua_pushnil(L);
	lua_setglobal(L, module->name);
	luaL_unref(L, LUA_REGISTRYINDEX, (intptr_t)module->data);
	module->lib = NULL;
	return ret;
}

/** @internal Helper for retrieving layer Lua function by name. */
#define LAYER_FFI_CALL(ctx, name) \
	struct kr_module *module = (ctx)->api->data; \
	lua_State *L = module->lib; \
	lua_rawgeti(L, LUA_REGISTRYINDEX, (intptr_t)module->data); \
	lua_getfield(L, -1, "layer"); \
	lua_remove(L, -2); \
	lua_getfield(L, -1, (name)); \
	lua_remove(L, -2); \
	if (lua_isnil(L, -1)) { \
		lua_pop(L, 1); \
		return ctx->state; \
	} \
	lua_pushnumber(L, ctx->state);

static int l_ffi_layer_begin(knot_layer_t *ctx, void *module_param)
{
	LAYER_FFI_CALL(ctx, "begin");
	lua_pushlightuserdata(L, module_param);
	ctx->data = module_param;
	return l_ffi_call(L, 2);
}

static int l_ffi_layer_reset(knot_layer_t *ctx)
{
	LAYER_FFI_CALL(ctx, "reset");
	lua_pushlightuserdata(L, ctx->data);
	return l_ffi_call(L, 2);
}

static int l_ffi_layer_finish(knot_layer_t *ctx)
{
	LAYER_FFI_CALL(ctx, "finish");
	lua_pushlightuserdata(L, ctx->data);
	return l_ffi_call(L, 2);
}

static int l_ffi_layer_consume(knot_layer_t *ctx, knot_pkt_t *pkt)
{
	LAYER_FFI_CALL(ctx, "consume");
	lua_pushlightuserdata(L, ctx->data);
	lua_pushlightuserdata(L, pkt);
	return l_ffi_call(L, 3);
}

static int l_ffi_layer_produce(knot_layer_t *ctx, knot_pkt_t *pkt)
{
	LAYER_FFI_CALL(ctx, "produce");
	lua_pushlightuserdata(L, ctx->data);
	lua_pushlightuserdata(L, pkt);
	return l_ffi_call(L, 3);
}

static int l_ffi_layer_fail(knot_layer_t *ctx, knot_pkt_t *pkt)
{
	LAYER_FFI_CALL(ctx, "fail");
	lua_pushlightuserdata(L, ctx->data);
	lua_pushlightuserdata(L, pkt);
	return l_ffi_call(L, 3);
}

/** @internal Retrieve C layer api wrapper. */
static const knot_layer_api_t* l_ffi_layer(struct kr_module *module)
{
	lua_State *L = module->lib;
	lua_rawgeti(L, LUA_REGISTRYINDEX, (intptr_t)module->data);
	lua_getfield(L, -1, "_layer_capi");
	knot_layer_api_t *api = lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (!api) {
		/* Fabricate layer API wrapping the Lua functions */
		knot_layer_api_t *api = malloc(sizeof(*api));
		if (api) {
			api->begin = l_ffi_layer_begin;
			api->finish = l_ffi_layer_finish;
			api->consume = l_ffi_layer_consume;
			api->produce = l_ffi_layer_produce;
			api->reset = l_ffi_layer_reset;
			api->fail = l_ffi_layer_fail;
			api->data = module;
		}
		/* Store the api in the registry. */
		lua_pushlightuserdata(L, api);
		lua_setfield(L, -2, "_layer_capi");
	}
	lua_pop(L, 1); /* Clear module table */
	return api;
}

#undef LAYER_FFI_CALL

/** @internal Helper macro for function presence check. */
#define REGISTER_FFI_CALL(L, attr, name, cb) do { \
	lua_getfield((L), -1, (name)); \
	if (!lua_isnil((L), -1)) { attr = cb; } \
	lua_pop((L), 1); \
	} while (0)

int ffimodule_register_lua(struct engine *engine, struct kr_module *module, const char *name)
{
	/* Register module in Lua */
	lua_State *L = engine->L;
	lua_getglobal(L, "require");
	lua_pushstring(L, name);
	if (lua_pcall(L, 1, LUA_MULTRET, 0) != 0) {
		fprintf(stderr, "error: %s\n", lua_tostring(L, -1));
		lua_pop(L, 1);
		return kr_error(ENOENT);
	}
	lua_setglobal(L, name);
	lua_getglobal(L, name);

	/* Create FFI module with trampolined functions. */
	memset(module, 0, sizeof(*module));
	module->name = strdup(name);
	REGISTER_FFI_CALL(L, module->init,   "init",   &l_ffi_init);
	REGISTER_FFI_CALL(L, module->deinit, "deinit", &l_ffi_deinit);
	REGISTER_FFI_CALL(L, module->layer,  "layer",  &l_ffi_layer);
	module->data = (void *)(intptr_t)luaL_ref(L, LUA_REGISTRYINDEX);
	module->lib = L;
	lua_pop(L, 1); /* Clear the module global */
	return module->init(module);
}

#undef REGISTER_FFI_CALL