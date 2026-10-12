/* Sandstone Lua hook: optional source preprocessing via Lua.
 *
 * The hook script must define `preprocess(source, name)` returning the
 * transformed source. liblua is loaded at run time with dlopen() so the
 * assembler framework links and runs without Lua; when no script (or no
 * liblua) is present the hook is the identity function.
 */
#include "intern.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#if defined(__has_include)
#if __has_include(<lua.h>)
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#define SANDSTONE_HAS_LUA_HEADERS 1
#endif
#endif

#if !defined(SANDSTONE_HAS_LUA_HEADERS)
#include <stddef.h>
typedef struct lua_State lua_State;
#endif

#include <dlfcn.h>

struct lua_api {
    void *handle;
    lua_State *(*L_newstate)(void);
    void (*L_close)(lua_State *);
    void (*L_openlibs)(lua_State *);
    int (*L_loadfilex)(lua_State *, const char *, const char *);
    int (*L_pcallk)(lua_State *, int, int, int, long, void *);
    void (*getglobal)(lua_State *, const char *);
    void (*pushstring)(lua_State *, const char *);
    void (*pushvalue)(lua_State *, int);
    void (*settable)(lua_State *, int);
    void (*rotate)(lua_State *, int, int);
    int (*isstring)(lua_State *, int);
    const char *(*tolstring)(lua_State *, int, size_t *);
    void (*settop)(lua_State *, int);
    int (*L_loadstring)(lua_State *, const char *);
    void (*pushcclosure)(lua_State *, int (*)(lua_State *), int);
    void (*setglobal)(lua_State *, const char *);
    int (*type)(lua_State *, int);
    int (*getfield)(lua_State *, int, const char *);
    void (*createtable)(lua_State *, int, int);
    void (*setfield)(lua_State *, int, const char *);
    int (*gettop)(lua_State *);
    void (*pushnumber)(lua_State *, double);
    void (*pushnil)(lua_State *);
    long long (*tointegerx)(lua_State *, int, int *);
    size_t (*rawlen)(lua_State *, int);
    int (*rawgeti)(lua_State *, int, long long);
    void (*rawseti)(lua_State *, int, long long);
    const char *(*pushfstring)(lua_State *, const char *, ...);
};

static struct lua_api g_lua;
static int g_lua_tried = 0;
typedef struct { char *p; size_t n, cap; } lua_textbuf;
static lua_textbuf *g_injected;
static int append_injected(const char *s,size_t n) {
    if(!g_injected || n>SIZE_MAX-g_injected->n-1)return 0;
    size_t need=g_injected->n+n+1;
    if(need>g_injected->cap){size_t c=g_injected->cap?g_injected->cap:128;while(c<need)c*=2;char*p=realloc(g_injected->p,c);if(!p)return 0;g_injected->p=p;g_injected->cap=c;}
    memcpy(g_injected->p+g_injected->n,s,n);g_injected->n+=n;g_injected->p[g_injected->n]=0;return 1;
}
static const char *lua_string(lua_State *L,int i,size_t *n) { return g_lua.tolstring(L,i,n); }
static int inject_value(lua_State *L,int i) {
    int t=g_lua.type(L,i);
    if(t==4 || t==3){size_t n=0;const char*s=lua_string(L,i,&n);return s?append_injected(s,n):0;}
    if(t==5){size_t n=g_lua.rawlen(L,i);for(size_t k=1;k<=n;k++){if(k>1)append_injected(", ",2);g_lua.rawgeti(L,i,(long long)k);int ok=inject_value(L,-1);g_lua.settop(L,-2);if(!ok)return 0;}return 1;}
    return 0;
}
static int lua_inject(lua_State *L) { return inject_value(L,1)?0:0; }
static int lua_injectf(lua_State *L) {
    size_t n=0;const char *fmt=lua_string(L,1,&n);if(!fmt)return 0;
    int arg=2; size_t table_index=1;
    for(size_t i=0;i<n;i++){
        if((fmt[i]=='~' || fmt[i]=='%') && i+1<n && fmt[i+1]!=fmt[i]){
            char marker=fmt[i],code=fmt[++i];
            if(marker=='~' && code=='l' && i+1<n) code=fmt[++i];
            if(code=='s' || code=='d' || code=='i' || code=='x'){
                int index=arg, pop=0;
                if(g_lua.type(L,arg)==5){
                    size_t count=g_lua.rawlen(L,arg);
                    if(table_index>count){arg++;table_index=1;index=arg;}
                    else {g_lua.rawgeti(L,arg,(long long)table_index++);index=-1;pop=1;}
                } else arg++;
                if(g_lua.type(L,index)==4 || code=='s'){size_t z=0;const char*v=lua_string(L,index,&z);if(v)append_injected(v,z);}
                else if(g_lua.type(L,index)==3){int valid=0;long long value=g_lua.tointegerx(L,index,&valid);char num[64];int z=code=='x'?snprintf(num,sizeof num,"%llx",value):snprintf(num,sizeof num,"%lld",value);if(z>0)append_injected(num,(size_t)z);}
                if(pop)g_lua.settop(L,-2);
                continue;
            }
            append_injected("~",1);append_injected(&code,1);continue;
        }
        append_injected(fmt+i,1);
    }
    return 0;
}

static void set_string(lua_State *L,int table,const char *key,const char *value) {
    g_lua.pushstring(L,value?value:"");g_lua.setfield(L,table,key);
}
static void set_number(lua_State *L,int table,const char *key,uint32_t value) {
    g_lua.pushnumber(L,(double)value);g_lua.setfield(L,table,key);
}
static void push_metadata(lua_State *L,DomSymAs_Bank *bank,int kind) {
    uint32_t count=kind==0?domsymas_feature_count(bank):kind==1?domsymas_register_count(bank):kind==2?domsymas_syntax_count(bank):kind==3?domsymas_section_count(bank):kind==4?domsymas_reloc_count(bank):domsymas_linkage_count(bank);
    g_lua.createtable(L,0,(int)count);
    for(uint32_t i=0;i<count;i++) {
        const char *name=NULL;g_lua.createtable(L,0,8);
        if(kind==0){const DomSymAs_Feature*r=domsymas_feature_at(bank,i);if(!r){g_lua.settop(L,-2);continue;}name=r->name;set_number(L,-2,"id",r->id);set_number(L,-2,"flags",r->flags);set_string(L,-2,"description",r->desc);}
        else if(kind==1){const DomSymAs_Register*r=domsymas_register_at(bank,i);if(!r){g_lua.settop(L,-2);continue;}name=r->name;set_number(L,-2,"id",r->id);set_number(L,-2,"class",r->regclass);set_number(L,-2,"width",r->width);set_number(L,-2,"encoding",r->enc_num);set_number(L,-2,"parent",r->parent);}
        else if(kind==2){const DomSymAs_Syntax*r=domsymas_syntax_at(bank,i);if(!r){g_lua.settop(L,-2);continue;}name=r->name;set_number(L,-2,"id",r->id);set_number(L,-2,"operand_order",r->operand_order);set_string(L,-2,"register_prefix",r->reg_prefix);set_string(L,-2,"immediate_prefix",r->imm_prefix);set_string(L,-2,"comment",r->comment);set_string(L,-2,"separator",r->separator);}
        else if(kind==3){const DomSymAs_SectionInfo*r=domsymas_section_at(bank,i);if(!r){g_lua.settop(L,-2);continue;}name=r->name;set_number(L,-2,"id",r->id);set_number(L,-2,"flags",r->flags);set_number(L,-2,"alignment",r->align);set_number(L,-2,"address_space",r->addrspace);set_number(L,-2,"content",r->content);}
        else if(kind==4){const DomSymAs_Reloc*r=domsymas_reloc_at(bank,i);if(!r){g_lua.settop(L,-2);continue;}name=r->name;set_number(L,-2,"id",r->id);set_number(L,-2,"width",r->width);set_number(L,-2,"offset",r->offset);set_number(L,-2,"signed",r->is_signed);set_number(L,-2,"pc_relative",r->pcrel);set_string(L,-2,"expression",r->expr);}
        else {const DomSymAs_Linkage*r=domsymas_linkage_at(bank,i);if(!r){g_lua.settop(L,-2);continue;}name=r->name;set_number(L,-2,"id",r->id);set_number(L,-2,"binding",r->binding);set_number(L,-2,"visibility",r->visibility);set_number(L,-2,"symbol_type",r->symtype);set_number(L,-2,"flags",r->flags);}
        g_lua.pushstring(L,name?name:"");g_lua.rotate(L,-2,1);g_lua.settable(L,-3);
    }
}
static void push_symas(lua_State *L,DomSymAs_Bank *bank) {
    g_lua.createtable(L,0,7); /* symas */
    g_lua.createtable(L,0,(int)domsymas_target_count(bank)); /* targets */
    for(uint32_t ti=0;ti<domsymas_target_count(bank);ti++) {
        const DomSymAs_Target *target=domsymas_target_at(bank,ti);
        if(!target)continue;
        g_lua.pushstring(L,target->arch?target->arch:"");
        g_lua.createtable(L,0,6);
        set_string(L,-2,"arch",target->arch);set_string(L,-2,"subarch",target->subarch);
        set_number(L,-2,"addr_size",target->addr_size);set_number(L,-2,"default_op_size",target->default_op_size);
        set_number(L,-2,"default_addr_size",target->default_addr_size);set_string(L,-2,"obj_format",target->obj_format);
        g_lua.createtable(L,0,(int)domsymas_mnemonic_count(bank));
        for(uint32_t mi=0;mi<domsymas_mnemonic_count(bank);mi++) {
            const DomSymAs_Mnemonic *m=domsymas_mnemonic_at(bank,mi);if(!m||!m->canonical)continue;
            g_lua.pushstring(L,m->canonical);g_lua.createtable(L,0,3);
            set_string(L,-2,"name",m->canonical);set_number(L,-2,"id",m->id);
            g_lua.createtable(L,(int)m->form_count,0);
            for(uint32_t fi=0;fi<m->form_count;fi++) {
                const DomSymAs_Form *form=domsymas_find_form(bank,m->forms[fi]);if(!form)continue;
                g_lua.createtable(L,0,3);set_number(L,-2,"id",form->id);set_number(L,-2,"operand_count",form->operand_count);
                g_lua.createtable(L,(int)domsymas_operand_count(bank,form->id),0);
                for(uint32_t oi=0;oi<domsymas_operand_count(bank,form->id);oi++) {
                    const DomSymAs_Operand *op=domsymas_operand_at(bank,form->id,oi);if(!op)continue;
                    g_lua.createtable(L,0,5);set_number(L,-2,"position",op->position);set_number(L,-2,"kind",op->kind);
                    set_number(L,-2,"width",op->width);set_string(L,-2,"constraint",op->constraint);
                    g_lua.rawseti(L,-2,(long long)oi+1);
                }
                g_lua.setfield(L,-2,"operands");g_lua.rawseti(L,-2,(long long)fi+1);
            }
            g_lua.setfield(L,-2,"forms");g_lua.setfield(L,-2,m->canonical);
        }
        g_lua.setfield(L,-2,"opcodes");g_lua.settable(L,-3);
    }
    g_lua.setfield(L,-2,"targets");
    push_metadata(L,bank,0);g_lua.setfield(L,-2,"features");
    push_metadata(L,bank,1);g_lua.setfield(L,-2,"registers");
    push_metadata(L,bank,2);g_lua.setfield(L,-2,"syntaxes");
    push_metadata(L,bank,3);g_lua.setfield(L,-2,"sections");
    push_metadata(L,bank,4);g_lua.setfield(L,-2,"relocations");
    push_metadata(L,bank,5);g_lua.setfield(L,-2,"linkages");
}

static void *load_sym(void *handle, const char *name) {
    dlerror();
    void *sym = dlsym(handle, name);
    return dlerror() ? NULL : sym;
}

/* Resolve the handful of entry points used below. */
static int lua_boot(struct lua_api *api) {
    static const char *const libs[] = {
        "liblua5.5.so", "liblua5.5.so.0", "liblua5.4.so", "liblua5.4.so.5", "liblua.so",
        "libluajit-5.1.so.2", "libluajit-5.1.so", NULL,
    };
    for (int i = 0; libs[i]; i++) {
        api->handle = dlopen(libs[i], RTLD_NOW | RTLD_LOCAL);
        if (api->handle)
            break;
    }
    if (!api->handle)
        return 0;
#define NEED(field, sym)                                                   \
    api->field = load_sym(api->handle, sym);                               \
    if (!api->field) {                                                     \
        dlclose(api->handle);                                              \
        api->handle = NULL;                                                \
        return 0;                                                          \
    }
    NEED(L_newstate, "luaL_newstate");
    NEED(L_close, "lua_close");
    NEED(L_openlibs, "luaL_openlibs");
    NEED(L_loadfilex, "luaL_loadfilex");
    NEED(L_pcallk, "lua_pcallk");
    NEED(getglobal, "lua_getglobal");
    NEED(pushstring, "lua_pushstring");
    NEED(pushvalue, "lua_pushvalue");
    NEED(settable, "lua_settable");
    NEED(rotate, "lua_rotate");
    NEED(isstring, "lua_isstring");
    NEED(tolstring, "lua_tolstring");
    NEED(settop, "lua_settop");
    NEED(L_loadstring, "luaL_loadstring");
    NEED(pushcclosure, "lua_pushcclosure");
    NEED(setglobal, "lua_setglobal");
    NEED(type, "lua_type");
    NEED(getfield, "lua_getfield");
    NEED(createtable, "lua_createtable");
    NEED(setfield, "lua_setfield");
    NEED(gettop, "lua_gettop");
    NEED(pushnumber, "lua_pushnumber");
    NEED(pushnil, "lua_pushnil");
    NEED(tointegerx, "lua_tointegerx");
    NEED(rawlen, "lua_rawlen");
    NEED(rawgeti, "lua_rawgeti");
    NEED(rawseti, "lua_rawseti");
#undef NEED
    return 1;
}

int sandstone_asm_load_hook(sandstone_asm_t *ctx, const char *lua_path) {
    free(ctx->hook_path);
    ctx->hook_path = NULL;
    if (!lua_path || !*lua_path)
        return 0;
    /* Validate early: the file must exist and parse under Lua. */
    if (!g_lua_tried) {
        g_lua_tried = 1;
        lua_boot(&g_lua);
    }
    if (!g_lua.handle) {
        sand_set_error(ctx, "Lua unavailable (no liblua found)");
        return -1;
    }
    lua_State *L = g_lua.L_newstate();
    if (!L) {
        sand_set_error(ctx, "cannot create Lua state");
        return -1;
    }
    g_lua.L_openlibs(L);
    int rc = g_lua.L_loadfilex(L, lua_path, NULL);
    if (rc == 0)
        rc = g_lua.L_pcallk(L, 0, 0, 0, 0, NULL);
    if (rc != 0) {
        size_t n = 0;
        const char *msg = g_lua.isstring(L, -1)
                              ? g_lua.tolstring(L, -1, &n)
                              : "lua error";
        sand_set_error(ctx, "hook '%s': %s", lua_path, msg);
        g_lua.L_close(L);
        return -1;
    }
    g_lua.L_close(L);
    ctx->hook_path = sand_strdup(lua_path);
    return ctx->hook_path ? 0 : -1;
}

char *sand_run_hook(sandstone_asm_t *ctx, const char *source,
                    const char *src_name) {    if (!ctx->hook_path)
        return sand_strdup(source);
    if (!g_lua.handle) {
        sand_set_error(ctx, "Lua unavailable (no liblua found)");
        return NULL;
    }
    lua_State *L = g_lua.L_newstate();
    if (!L)
        return NULL;
    g_lua.L_openlibs(L);
    char *out = NULL;
    int rc = g_lua.L_loadfilex(L, ctx->hook_path, NULL);
    if (rc == 0)
        rc = g_lua.L_pcallk(L, 0, 0, 0, 0, NULL);
    if (rc == 0) {
        g_lua.getglobal(L, "preprocess");
        g_lua.pushstring(L, source);
        g_lua.pushstring(L, src_name);
        rc = g_lua.L_pcallk(L, 2, 1, 0, 0, NULL);
    }
    if (rc == 0 && g_lua.isstring(L, -1)) {
        size_t n = 0;
        const char *s = g_lua.tolstring(L, -1, &n);
        out = sand_strndup(s ? s : "", n);
    } else {
        size_t n = 0;
        const char *msg = g_lua.isstring(L, -1)
                              ? g_lua.tolstring(L, -1, &n)
                              : "lua error";
        sand_set_error(ctx, "hook '%s': %s", ctx->hook_path, msg);
    }
    g_lua.L_close(L);
    return out;
}

char *sandstone_asm_preprocess(sandstone_asm_t *ctx, const char *source) {
    if (!ctx || !source)
        return NULL;
    return sand_run_hook(ctx, source, "<preprocess>");
}

char *sand_run_lua_block(sandstone_asm_t *ctx,const char *code) {
    if(!g_lua_tried){g_lua_tried=1;lua_boot(&g_lua);}
    if(!g_lua.handle){sand_set_error(ctx,"Lua unavailable (no compatible liblua found)");return NULL;}
    lua_State *L=(lua_State*)ctx->lua_state;
    if(!L){L=g_lua.L_newstate();if(!L){sand_set_error(ctx,"cannot create Lua state");return NULL;}
        g_lua.L_openlibs(L);g_lua.createtable(L,0,3);
        g_lua.pushcclosure(L,lua_inject,0);g_lua.setfield(L,-2,"inject");
        g_lua.pushcclosure(L,lua_injectf,0);g_lua.setfield(L,-2,"injectf");
        push_symas(L,ctx->bank);g_lua.setfield(L,-2,"symas");g_lua.setglobal(L,"lsandas");ctx->lua_state=L;}
    lua_textbuf output={0}; g_injected=&output;
    int rc=g_lua.L_loadstring(L,code?code:""); if(rc==0)rc=g_lua.L_pcallk(L,0,0,0,0,NULL);
    if(rc!=0){size_t n=0;const char *msg=g_lua.isstring(L,-1)?g_lua.tolstring(L,-1,&n):"Lua block failed";sand_set_error(ctx,"Lua block: %s",msg?msg:"Lua block failed");free(output.p);output.p=NULL;}
    g_injected=NULL;
    if(rc!=0)return NULL;
    if(!output.p) output.p=sand_strdup("");
    return output.p;
}

void sand_lua_reset(sandstone_asm_t *ctx) {
    if(!ctx || !ctx->lua_state)return;
    if(!g_lua_tried){g_lua_tried=1;lua_boot(&g_lua);}
    if(g_lua.handle)g_lua.L_close((lua_State*)ctx->lua_state);
    ctx->lua_state=NULL;
}
