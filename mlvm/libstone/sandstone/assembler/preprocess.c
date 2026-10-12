/* Source-level expansion for SandAS.  This deliberately runs before the
 * instruction parser so macros and conditional source never leak to the
 * SymAS encoder. */
#include "intern.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct { char *p; size_t n, cap; } textbuf;
typedef struct { char *name, *params, *body; } macro;
typedef struct { char *name; int64_t value; } pp_define;

static int append(textbuf *b, const char *s, size_t n) {
    if (n > SIZE_MAX - b->n - 1) return 0;
    size_t need = b->n + n + 1;
    if (need > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < need) cap *= 2;
        char *p = realloc(b->p, cap);
        if (!p) return 0;
        b->p = p; b->cap = cap;
    }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
    return 1;
}
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n-1])) s[--n] = 0;
    return s;
}
static int word(const char *s, const char *w) {
    size_t n = strlen(w);
    return !strncmp(s,w,n) && (!s[n] || isspace((unsigned char)s[n]));
}
static int emit_equ(textbuf *out,const char *name,int64_t value) {
    char line[512]; int n=snprintf(line,sizeof line,".equ %s, %lld\n",name,(long long)value);
    return n>0 && (size_t)n<sizeof line && append(out,line,(size_t)n);
}
static int eval_pp(sandstone_asm_t *ctx,const char *expr,pp_define *defs,size_t ndefs,int64_t *value) {
    textbuf expanded={0};
    for(const char *p=expr?expr:"";*p;) {
        if(isalpha((unsigned char)*p)||*p=='_'||*p=='.'||*p=='$') {
            const char *e=p+1;while(isalnum((unsigned char)*e)||*e=='_'||*e=='.'||*e=='$')e++;
            char *name=sand_strndup(p,(size_t)(e-p));int found=0;
            for(size_t k=0;k<ndefs;k++)if(!strcmp(name,defs[k].name)){char number[64];int n=snprintf(number,sizeof number,"%lld",(long long)defs[k].value);append(&expanded,number,(size_t)n);found=1;break;}
            if(!found)append(&expanded,p,(size_t)(e-p));
            free(name);p=e;
        } else {append(&expanded,p,1);p++;}
    }
    int ok=0;*value=sand_eval_expr(ctx,expanded.p?expanded.p:"",&ok);free(expanded.p);return ok;
}
static char *read_file(const char *path) {
    FILE *f=fopen(path,"rb"); if(!f)return NULL;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;}
    long n=ftell(f); if(n<0 || n>16*1024*1024){fclose(f);return NULL;}
    rewind(f); char *p=malloc((size_t)n+1); if(!p){fclose(f);return NULL;}
    size_t got=fread(p,1,(size_t)n,f); fclose(f);
    if(got!=(size_t)n){free(p);return NULL;} p[n]=0; return p;
}
static char *expand_includes(sandstone_asm_t *ctx,const char *src,
                             const char *src_name,unsigned depth) {
    if(depth>32){sand_set_error(ctx,"include nesting exceeds 32 files");return NULL;}
    textbuf out={0}; char *copy=sand_strdup(src?src:""); if(!copy)return NULL;
    char *dir=sand_strdup(src_name?src_name:"");
    if(dir){char *slash=strrchr(dir,'/');if(slash)slash[1]=0;else dir[0]=0;}
    for(char *save=NULL,*line=strtok_r(copy,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
        char *t=sand_strdup(line); if(!t)goto fail; char *s=trim(t);
        if(word(s,".include")) {
            char *arg=trim(s+8); size_t n=strlen(arg);
            if(n<2 || !((arg[0]=='"'&&arg[n-1]=='"')||(arg[0]=='<'&&arg[n-1]=='>'))) {
                free(t);sand_set_error(ctx,"malformed .include (expected quoted path)");goto fail;
            }
            char *name=sand_strndup(arg+1,n-2); if(!name){free(t);goto fail;}
            char *path=NULL;
            if(name[0]=='/' || !dir || !*dir) path=sand_strdup(name);
            else {size_t z=strlen(dir)+strlen(name)+1;path=malloc(z);if(path)snprintf(path,z,"%s%s",dir,name);}
            char *included=path?read_file(path):NULL;
            if(!included){sand_set_error(ctx,"cannot read include '%s'",path?path:name);free(name);free(path);free(t);goto fail;}
            char *nested=expand_includes(ctx,included,path,depth+1);
            free(included);free(name);free(path);free(t);
            if(!nested || !append(&out,nested,strlen(nested))){free(nested);goto fail;} free(nested);
        } else { if(!append(&out,line,strlen(line))||!append(&out,"\n",1)){free(t);goto fail;} free(t); }
    }
    free(dir);free(copy);return out.p?out.p:sand_strdup("");
fail: free(dir);free(copy);free(out.p);return NULL;
}
static int replace_lines(char ***lines,size_t *nl,size_t *cap,size_t first,
                         size_t last,const char *text) {
    char *copy=sand_strdup(text?text:"");if(!copy)return 0;
    char **items=NULL;size_t n=0,c=0;
    for(char *save=NULL,*p=strtok_r(copy,"\n",&save);p;p=strtok_r(NULL,"\n",&save)) {
        if(n==c){c=c?c*2:8;char**q=realloc(items,c*sizeof *q);if(!q)goto bad;items=q;}
        items[n]=sand_strdup(p);if(!items[n])goto bad;n++;
    }
    if(!n){items=malloc(sizeof *items);if(!items)goto bad;items[0]=sand_strdup("");if(!items[0])goto bad;n=1;}
    size_t removed=last-first+1, newn=*nl-removed+n;
    if(newn>*cap){size_t nc=*cap?*cap:64;while(nc<newn)nc*=2;char**q=realloc(*lines,nc*sizeof *q);if(!q)goto bad;*lines=q;*cap=nc;}
    for(size_t i=first;i<=last;i++)free((*lines)[i]);
    memmove(*lines+first+n,*lines+last+1,(*nl-last-1)*sizeof **lines);
    memcpy(*lines+first,items,n*sizeof *items);*nl=newn;
    free(items);free(copy);return 1;
bad: if(items){for(size_t k=0;k<n;k++)free(items[k]);free(items);}free(copy);return 0;
}
typedef struct { char *from,*to; } local_name;
static char *hygienize(const char *body,unsigned id) {
    char *copy=sand_strdup(body);if(!copy)return NULL;local_name names[32];size_t nn=0;
    for(char *save=NULL,*line=strtok_r(copy,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
        char *v=trim(line);if(strncmp(v,"local",5)||!isspace((unsigned char)v[5]))continue;
        v=trim(v+5);char *end=v;while(isalnum((unsigned char)*end)||*end=='_'||*end=='.')end++;
        if(end==v||nn==32) continue;
        char *name=sand_strndup(v,(size_t)(end-v));char generated[256];snprintf(generated,sizeof generated,"__sand_macro_%u_%s",id,name);
        names[nn++]=(local_name){name,sand_strdup(generated)};
    }
    free(copy);copy=sand_strdup(body);if(!copy)goto fail;
    textbuf out={0};char *p=copy;
    while(*p){char *line_end=strchr(p,'\n');size_t line_n=line_end?(size_t)(line_end-p):strlen(p);
        char *line=sand_strndup(p,line_n);if(!line)goto fail_out;char *v=trim(line);
        if(strncmp(v,"local",5)||!isspace((unsigned char)v[5])) {
            for(size_t k=0;k<line_n;){
                if(isalpha((unsigned char)p[k])||p[k]=='_'||p[k]=='.'){
                    size_t e=k+1;while(e<line_n&&(isalnum((unsigned char)p[e])||p[e]=='_'||p[e]=='.'||p[e]=='$'))e++;
                    int found=0;for(size_t j=0;j<nn;j++)if(strlen(names[j].from)==e-k&&!strncmp(p+k,names[j].from,e-k)){append(&out,names[j].to,strlen(names[j].to));found=1;break;}
                    if(!found)append(&out,p+k,e-k);
                    k=e;
                } else {append(&out,p+k,1);k++;}
            }
            if(line_end)append(&out,"\n",1);
        }
        free(line);p+=line_n+(line_end?1:0);
    }
    free(copy);for(size_t k=0;k<nn;k++){free(names[k].from);free(names[k].to);}return out.p?out.p:sand_strdup("");
fail_out: free(out.p);free(copy);
fail: for(size_t k=0;k<nn;k++){free(names[k].from);free(names[k].to);}return NULL;
}
static char *substitute(const char *body, char **args, size_t nargs,
                       char **names, size_t nname) {
    textbuf out = {0};
    for (const char *p=body; *p;) {
        if (p[0]=='\\' && isdigit((unsigned char)p[1])) {
            unsigned idx=(unsigned)(p[1]-'1');
            if (idx<nargs && !append(&out,args[idx],strlen(args[idx]))) goto fail;
            p+=2;
        } else if (p[0]=='\\' && p[1]=='\\') {
            if (!append(&out,"\\",1)) goto fail;
            p+=2;
        } else if (p[0]=='\\' && (isalpha((unsigned char)p[1]) || p[1]=='_')) {
            const char *end=p+1;
            while (isalnum((unsigned char)*end) || *end=='_') end++;
            size_t len=(size_t)(end-p-1); int found=0;
            for(size_t i=0;i<nname;i++) if(strlen(names[i])==len && !strncmp(p+1,names[i],len)) {
                if(i<nargs && !append(&out,args[i],strlen(args[i]))) goto fail;
                found=1; break;
            }
            if(!found && !append(&out,p,(size_t)(end-p))) goto fail;
            p=end;
        } else { if (!append(&out,p++,1)) goto fail; }
    }
    return out.p ? out.p : sand_strdup("");
fail: free(out.p); return NULL;
}

char *sand_preprocess_source(sandstone_asm_t *ctx, const char *src,
                             const char *src_name) {
    char *copy=expand_includes(ctx,src,src_name,0);
    if (!copy) return NULL;
    char **lines=NULL; size_t nl=0, lc=0;
    for (char *save=NULL,*p=strtok_r(copy,"\n",&save); p; p=strtok_r(NULL,"\n",&save)) {
        if (nl==lc) { lc=lc?lc*2:64; char **q=realloc(lines,lc*sizeof *q); if(!q) goto fail; lines=q; }
        lines[nl++]=sand_strdup(p); if(!lines[nl-1]) goto fail;
    }
    macro *macros=NULL; size_t nm=0, mc=0;
    textbuf out={0}; int active=1, cond[64], depth=0; unsigned expansions=0;
    pp_define defs[256];size_t ndefs=0;
    for (size_t i=0;i<nl;i++) {
        char *line=sand_strdup(lines[i]); if(!line) goto fail_all;
        char *s=trim(line);
        if(active && (word(s,".equ")||word(s,".set"))) {
            char *args=trim(s+4),*comma=strchr(args,',');
            if(comma){*comma=0;char *name=trim(args);int64_t value=0;
                if(eval_pp(ctx,trim(comma+1),defs,ndefs,&value)) {
                    size_t k;for(k=0;k<ndefs;k++)if(!strcmp(defs[k].name,name))break;
                    if(k==ndefs && ndefs<256)defs[ndefs++].name=sand_strdup(name);
                    if(k<ndefs)defs[k].value=value;
                }
            }
        }
        if(word(s,".if")) { if(depth==64){free(line);sand_set_error(ctx,"conditional nesting too deep");goto fail_all;} cond[depth++]=active; int64_t v=0; int ok=eval_pp(ctx,trim(s+3),defs,ndefs,&v); active=active && ok && v!=0; free(line);continue; }
        if(word(s,".else")) { if(!depth){free(line);sand_set_error(ctx,"unexpected .else");goto fail_all;} active=cond[depth-1] && !active; free(line);continue; }
        if(word(s,".endif")) { if(!depth){free(line);sand_set_error(ctx,"unexpected .endif");goto fail_all;} active=cond[--depth]; free(line);continue; }
        if(!active) {
            if(word(s,".macro") || word(s,".struct") || word(s,".enum") || word(s,".rept")) {
                const char *end=word(s,".macro")?".endmacro":word(s,".struct")?".endstruct":word(s,".enum")?".endenum":".endr";
                for(size_t j=i+1;j<nl;j++)if(word(trim(lines[j]),end)){i=j;break;}
            } else if(!strncmp(s,"<:",2) && !strstr(s+2,":>")) {
                for(size_t j=i+1;j<nl;j++)if(strstr(lines[j],":>")){i=j;break;}
            }
            free(line);continue;
        }
        if (word(s,".macro")) {
            char *head=trim(s+6), *sp=head;
            while (*sp && !isspace((unsigned char)*sp) && *sp!=',') sp++;
            char save=*sp; *sp=0; char *name=sand_strdup(head); *sp=save;
            char *params=sand_strdup(trim(sp)); textbuf body={0};
            size_t j=i+1; unsigned nested=0;
            for (;j<nl;j++) {
                char *t=sand_strdup(lines[j]); if(!t) break;
                char *v=trim(t);
                if(word(v,".macro")) nested++;
                if(word(v,".endmacro")) { free(t); if(!nested) break; nested--; }
                append(&body,lines[j],strlen(lines[j])); append(&body,"\n",1); free(t);
            }
            if(j==nl || !name || !params || !body.p) { free(name);free(params);free(body.p);free(line);sand_set_error(ctx,"unterminated or invalid .macro");goto fail_all; }
            if(nm==mc){mc=mc?mc*2:8;macro*q=realloc(macros,mc*sizeof *q);if(!q){free(name);free(params);free(body.p);free(line);goto fail_all;}macros=q;}
            macros[nm++]=(macro){name,params,body.p}; i=j; free(line); continue;
        }
        if(word(s,".endmacro")){ sand_set_error(ctx,"unexpected .endmacro"); free(line);goto fail_all; }
        if(word(s,".enum")) {
            char *name=trim(s+5); int64_t next=0; size_t j=i+1;
            for(;j<nl && !word(trim(lines[j]),".endenum");j++) {
                char *entry=sand_strdup(lines[j]); if(!entry)break; char *v=trim(entry);
                if(!*v || *v==';'){free(entry);continue;}
                char *eq=strchr(v,'='); int64_t value=next;
                if(eq){*eq=0;if(!eval_pp(ctx,trim(eq+1),defs,ndefs,&value) && !sand_parse_integer(trim(eq+1),&value)){free(entry);sand_set_error(ctx,"invalid enum value");goto fail_all;}}
                char *entry_name=trim(v);
                if(!emit_equ(&out,entry_name,value)){free(entry);goto fail_all;}
                if(*name){char full[512];snprintf(full,sizeof full,"%s.%s",name,entry_name);if(!emit_equ(&out,full,value)){free(entry);goto fail_all;}}
                next=value+1;free(entry);
            }
            if(j==nl){free(line);sand_set_error(ctx,"unterminated .enum");goto fail_all;} i=j;free(line);continue;
        }
        if(word(s,".struct")) {
            char *name=sand_strdup(trim(s+7)); if(!name){free(line);goto fail_all;}
            uint64_t offset=0; size_t j=i+1;
            for(;j<nl && !word(trim(lines[j]),".endstruct");j++) {
                char *entry=sand_strdup(lines[j]); if(!entry)break; char *v=trim(entry);
                if(word(v,".field")) {
                    char *field=trim(v+6), *comma=strchr(field,','); int64_t sz=0;
                    if(!comma){free(entry);free(name);sand_set_error(ctx,".field expects name,size");goto fail_all;}
                    *comma=0; int ok=eval_pp(ctx,trim(comma+1),defs,ndefs,&sz);
                    if(!ok)ok=sand_parse_integer(trim(comma+1),&sz);
                    if(!ok || sz<0 || sz>UINT32_MAX){free(entry);free(name);sand_set_error(ctx,"invalid .field size");goto fail_all;}
                    char full[512];snprintf(full,sizeof full,"%s.%s",name,trim(field));
                    if(!emit_equ(&out,full,(int64_t)offset)){free(entry);free(name);goto fail_all;} offset+=(uint64_t)sz;
                } else if(word(v,".align")) {
                    int64_t a=0;int ok=eval_pp(ctx,trim(v+6),defs,ndefs,&a);if(!ok)ok=sand_parse_integer(trim(v+6),&a);
                    if(!ok || a<=0){free(entry);free(name);sand_set_error(ctx,"invalid struct alignment");goto fail_all;}
                    offset=(offset+(uint64_t)a-1)/(uint64_t)a*(uint64_t)a;
                }
                free(entry);
            }
            if(j==nl){free(name);free(line);sand_set_error(ctx,"unterminated .struct");goto fail_all;}
            char full[512];snprintf(full,sizeof full,"%s.size",name);emit_equ(&out,full,(int64_t)offset);free(name);i=j;free(line);continue;
        }
        if(!strncmp(s,"<:",2)) {
            char *close=strstr(s+2,":>"); textbuf code={0};
            if(close) append(&code,s+2,(size_t)(close-(s+2)));
            else { append(&code,s+2,strlen(s+2));append(&code,"\n",1);size_t j=i+1;
                for(;j<nl;j++){char *candidate=sand_strdup(lines[j]);if(!candidate)break;char*v=trim(candidate);char*end=strstr(v,":>");if(end){append(&code,v,(size_t)(end-v));free(candidate);break;}append(&code,lines[j],strlen(lines[j]));append(&code,"\n",1);free(candidate);}
                if(j==nl){free(code.p);free(line);sand_set_error(ctx,"unterminated Lua block");goto fail_all;}i=j;}
            char *generated=sand_run_lua_block(ctx,code.p?code.p:"");free(code.p);
            if(!generated){free(line);goto fail_all;}
            if(++expansions>100000 || !replace_lines(&lines,&nl,&lc,i,i,generated)){
                free(generated);free(line);sand_set_error(ctx,"source expansion limit or allocation failure");goto fail_all;
            }
            free(generated);free(line);i--;continue;
        }
        if(word(s,".rept")) {
            int64_t count=0; int ok=eval_pp(ctx,trim(s+5),defs,ndefs,&count); size_t j=i+1;
            unsigned nested=0;
            for(;j<nl;j++) {
                if(word(trim(lines[j]),".rept"))nested++;
                else if(word(trim(lines[j]),".endr")){if(!nested)break;nested--;}
            }
            if(j==nl || !ok || count<0 || count>100000){free(line);sand_set_error(ctx,"invalid .rept count or missing .endr");goto fail_all;}
            textbuf repeated={0};
            for(int64_t k=0;k<count;k++) for(size_t x=i+1;x<j;x++) { append(&repeated,lines[x],strlen(lines[x]));append(&repeated,"\n",1); }
            if(++expansions>100000 || !replace_lines(&lines,&nl,&lc,i,j,repeated.p?repeated.p:"")){free(repeated.p);free(line);sand_set_error(ctx,"source expansion limit or allocation failure");goto fail_all;}
            free(repeated.p);free(line);i--;continue;
        }
        if(word(s,".endr")){free(line);sand_set_error(ctx,"unexpected .endr");goto fail_all;}
        if(word(s,".include")) { sand_set_error(ctx,".include is not available for string input yet");free(line);goto fail_all; }
        char *mn=s; while(*mn&&!isspace((unsigned char)*mn)&&*mn!=',')mn++;
        char save=*mn;*mn=0; macro *m=NULL;
        for(size_t k=0;k<nm;k++) if(!strcmp(macros[k].name,s)){m=&macros[k];break;}
        *mn=save;
        if(m) {
            char *paramcopy=sand_strdup(m->params), *argtext=trim(mn);
            char *args[32]={0}, *names[32], *defaults[32]={0}; size_t na=0, nn=0;
            for(char *ps=paramcopy; ps && *ps && nn<32;) {
                char *comma=strchr(ps,','); if(comma)*comma=0;
                char *pn=trim(ps); char *eq=strchr(pn,'='); if(eq){*eq=0;defaults[nn]=trim(eq+1);}
                pn=trim(pn); size_t plen=strlen(pn);if(plen>=3&&!strcmp(pn+plen-3,"..."))pn[plen-3]=0;
                names[nn++]=pn;
                if(!comma) break;
                ps=comma+1;
            }
            while(*argtext && na<32){ char *comma=strchr(argtext,','); if(comma)*comma=0; args[na++]=trim(argtext); if(!comma)break;argtext=comma+1; }
            size_t actual=na; int variadic=0; char *varrest=NULL;
            if(m->params[0]) {
                char *last=strrchr(m->params,','); last=last?last+1:(char*)m->params;
                last=trim(last); size_t z=strlen(last); if(z>=3&&!strcmp(last+z-3,"..."))variadic=1;
            }
            if(variadic && nn && actual>=nn-1) {
                textbuf rest={0};for(size_t k=nn-1;k<actual;k++){if(k>nn-1)append(&rest,", ",2);append(&rest,args[k],strlen(args[k]));}
                varrest=rest.p?rest.p:sand_strdup("");args[nn-1]=varrest;na=nn;
            }
            for(size_t k=na;k<nn;k++) args[k]=defaults[k]?defaults[k]:"";
            if(na<nn)na=nn;
            char *hygienic=hygienize(m->body,expansions+1);
            char *expanded=hygienic?substitute(hygienic,args,na,names,nn):NULL;free(hygienic);free(varrest);free(paramcopy);
            if(!expanded || ++expansions>100000 || !replace_lines(&lines,&nl,&lc,i,i,expanded?expanded:"")){free(expanded);free(line);sand_set_error(ctx,"source expansion limit or allocation failure");goto fail_all;}
            free(expanded);free(line);i--;continue;
        } else { append(&out,lines[i],strlen(lines[i])); append(&out,"\n",1); }
        free(line);
    }
    if(depth){sand_set_error(ctx,"unterminated .if");goto fail_all;}
    for(size_t k=0;k<nm;k++){free(macros[k].name);free(macros[k].params);free(macros[k].body);} free(macros);
    for(size_t k=0;k<ndefs;k++)free(defs[k].name);
    for(size_t k=0;k<nl;k++) free(lines[k]);
    free(lines); free(copy);
    return out.p?out.p:sand_strdup("");
fail_all:
    for(size_t k=0;k<nm;k++){free(macros[k].name);free(macros[k].params);free(macros[k].body);} free(macros);free(out.p);
    for(size_t k=0;k<ndefs;k++)free(defs[k].name);
fail:
    if(lines){for(size_t k=0;k<nl;k++)free(lines[k]);free(lines);} free(copy); return NULL;
}
