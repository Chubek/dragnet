#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct runtime_host {
  limestone_binary_runtime *runtime;
  limestone_translated_region *active;
  unsigned installations,releases,calls;
  int install_mode,execute_mode,release_mode;
} runtime_host;
typedef struct executable { runtime_host *host;int64_t value; } executable;

static void release(void *data) {
  executable *code=(executable *)data;
  runtime_host *host=code->host;size_t invalidated=999;limestone_error error;
  ++host->releases;
  free(code);
  if(host->release_mode==1) {
    uint8_t bytes[]={1,1};host->release_mode=0;
    assert(limestone_runtime_resident_count(host->runtime)==1);
    assert(!limestone_runtime_prepare(host->runtime,bytes,2,500,600,&error)&&error.code==LIMESTONE_CONFLICT);
    assert(limestone_runtime_invalidate(host->runtime,300,2,&invalidated,&error)==LIMESTONE_OK&&invalidated==1);
  }else if(host->release_mode==2) {
    host->release_mode=0;
    assert(limestone_runtime_invalidate(host->runtime,300,2,&invalidated,&error)==LIMESTONE_OK&&invalidated==1);
    assert(limestone_runtime_resident_count(host->runtime)==0);
  }else if(host->release_mode==3) {
    uint8_t bytes[]={1,1};host->release_mode=0;
    assert(limestone_runtime_resident_count(host->runtime)==0);
    assert(limestone_runtime_invalidate(host->runtime,100,2,&invalidated,&error)==LIMESTONE_CONFLICT&&invalidated==999);
    assert(!limestone_runtime_prepare(host->runtime,bytes,2,500,600,&error)&&error.code==LIMESTONE_CONFLICT);
    assert(limestone_runtime_open_cache(host->runtime,"unused",1048576,&error)==LIMESTONE_CONFLICT);
  }
}
static limestone_status execute(void *data,int64_t *result,limestone_error *error) {
  executable *code=(executable *)data;runtime_host *host=code->host;
  assert(error&&error->code==LIMESTONE_OK&&error->message[0]==0);
  ++host->calls;
  if(host->execute_mode==1) {
    size_t invalidated;unsigned before=host->releases;
    assert(limestone_runtime_invalidate(host->runtime,100,2,&invalidated,NULL)==LIMESTONE_OK&&invalidated>0);
    limestone_translated_region_destroy(host->active);host->active=NULL;
    assert(host->releases==before); /* Active executable ownership is retained. */
  }else if(host->execute_mode==2) {
    error->code=LIMESTONE_TIMEOUT;strcpy(error->message,"host execution timed out");return LIMESTONE_TIMEOUT;
  }else if(host->execute_mode==3)return (limestone_status)12345;
  *result=code->value;
  return LIMESTONE_OK;
}
static limestone_status install(const limestone_runtime_region_view *view,limestone_runtime_executable *out,void *data,limestone_error *error) {
  runtime_host *host=(runtime_host *)data;executable *code;size_t k;
  assert(view&&view->guest_size==view->byte_count&&view->guest_size>0);
  for(k=0;k<view->guest_size;++k)assert(view->guest_bytes[k]==1&&view->bytes[k]==9);
  assert(!out->execute&&!out->release&&!out->userdata);
  assert(error&&error->code==LIMESTONE_OK&&error->message[0]==0);
  code=(executable *)malloc(sizeof(*code));assert(code);code->host=host;code->value=40+(int64_t)view->byte_count;
  out->execute=execute;out->release=release;out->userdata=code;++host->installations;
  if(host->install_mode==1) {strcpy(error->message,"host installation unsupported");return LIMESTONE_UNSUPPORTED;}
  if(host->install_mode==2)out->execute=NULL;
  if(host->install_mode==3) {
    size_t invalidated;
    assert(limestone_runtime_invalidate(host->runtime,view->guest_address,view->guest_size,&invalidated,NULL)==LIMESTONE_OK&&invalidated>0);
  }
  if(host->install_mode==4) {
    limestone_error nested;
    assert(!limestone_runtime_prepare(host->runtime,view->guest_bytes,view->guest_size,view->guest_address,view->target_address,&nested)&&nested.code==LIMESTONE_CONFLICT);
    assert(limestone_runtime_open_cache(host->runtime,"unused",65536,&nested)==LIMESTONE_CONFLICT);
  }
  if(host->install_mode==5) {memset(error->message,'x',sizeof(error->message));return LIMESTONE_INTERRUPTED;}
  return LIMESTONE_OK;
}
static limestone_binary_architecture *architecture(const char *directory,const char *name) {
  char path[4096],source[16384];size_t count;FILE *file;limestone_error error;
  snprintf(path,sizeof(path),"%s/%s",directory,name);file=fopen(path,"rb");assert(file);
  count=fread(source,1,sizeof(source)-1,file);assert(!ferror(file)&&feof(file));fclose(file);source[count]=0;
  {limestone_binary_architecture *result=limestone_binary_architecture_load(source,&error);assert(result);return result;}
}
static void release_reentrancy(const limestone_binary_architecture *source,const limestone_binary_architecture *target) {
  limestone_runtime_options options={1,1};runtime_host host={0};uint8_t bytes[]={1,1};limestone_translated_region *region;limestone_error error;size_t invalidated;
  host.runtime=limestone_runtime_create(source,target,&options,install,&host,&error);assert(host.runtime);
  region=limestone_runtime_prepare(host.runtime,bytes,2,100,200,&error);assert(region);limestone_translated_region_destroy(region);
  host.release_mode=1;assert(!limestone_runtime_prepare(host.runtime,bytes,2,300,400,&error)&&error.code==LIMESTONE_INTERRUPTED&&host.releases==1&&limestone_runtime_resident_count(host.runtime)==0);
  limestone_runtime_destroy(host.runtime);
  options.max_regions=2;host.runtime=limestone_runtime_create(source,target,&options,install,&host,&error);assert(host.runtime);
  region=limestone_runtime_prepare(host.runtime,bytes,2,100,200,&error);assert(region);limestone_translated_region_destroy(region);
  region=limestone_runtime_prepare(host.runtime,bytes,2,300,400,&error);assert(region);limestone_translated_region_destroy(region);
  host.release_mode=2;assert(limestone_runtime_invalidate(host.runtime,100,2,&invalidated,&error)==LIMESTONE_OK&&invalidated==1&&host.releases==3&&limestone_runtime_resident_count(host.runtime)==0);
  region=limestone_runtime_prepare(host.runtime,bytes,2,100,200,&error);assert(region);limestone_translated_region_destroy(region);
  host.release_mode=3;limestone_runtime_destroy(host.runtime);assert(host.releases==4&&host.installations==4);
}
void runtime_c_api(const char *directory) {
  limestone_binary_architecture *source=architecture(directory,"byte-source.isa"),*target=architecture(directory,"byte-target.isa");
  limestone_runtime_options options;limestone_runtime_region_view view;limestone_error error;
  runtime_host host={0};uint8_t bytes[]={1,1};int64_t result=-7;size_t invalidated=999;
  limestone_binary_runtime *foreign;limestone_translated_region *cold,*hot,*other;
  release_reentrancy(source,target);
  limestone_runtime_options_default(NULL);limestone_runtime_options_default(&options);assert(options.hot_threshold==10&&options.max_regions==1024);
  options.hot_threshold=0;assert(!limestone_runtime_create(source,target,&options,NULL,NULL,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
  options.hot_threshold=2;options.max_regions=1;
  host.runtime=limestone_runtime_create(source,target,&options,install,&host,&error);assert(host.runtime&&error.code==LIMESTONE_OK);
  foreign=limestone_runtime_create(source,target,NULL,NULL,NULL,NULL);assert(foreign);
  options.hot_threshold=99; /* Options and architectures are owning snapshots. */
  cold=limestone_runtime_prepare(host.runtime,bytes,2,100,200,&error);assert(cold&&limestone_translated_region_is_valid(cold)&&!limestone_translated_region_is_compiled(cold));
  assert(limestone_runtime_invoke(host.runtime,cold,&result,NULL)==LIMESTONE_UNSUPPORTED&&result==-7);
  assert(limestone_runtime_invoke(foreign,cold,&result,NULL)==LIMESTONE_INVALID_ARGUMENT&&result==-7);
  hot=limestone_runtime_prepare(host.runtime,bytes,2,100,200,&error);assert(hot&&limestone_translated_region_is_compiled(hot)&&!limestone_translated_region_is_compiled(cold)&&host.installations==1);
  bytes[0]=99;assert(limestone_translated_region_get_view(hot,&view,&error)==LIMESTONE_OK&&view.guest_bytes[0]==1&&view.bytes[0]==9&&view.byte_count==2&&view.guest_address==100&&view.target_address==200);bytes[0]=1;
  host.execute_mode=2;assert(limestone_runtime_invoke(host.runtime,hot,&result,&error)==LIMESTONE_TIMEOUT&&strcmp(error.message,"host execution timed out")==0&&result==-7);
  host.execute_mode=3;assert(limestone_runtime_invoke(host.runtime,hot,&result,&error)==LIMESTONE_INTERNAL&&result==-7);
  host.execute_mode=0;assert(limestone_runtime_invoke(host.runtime,hot,&result,NULL)==LIMESTONE_OK&&result==42);
  other=limestone_runtime_prepare(host.runtime,bytes,2,300,400,&error);assert(other&&limestone_runtime_resident_count(host.runtime)==1);
  assert(limestone_runtime_invoke(host.runtime,hot,&result,NULL)==LIMESTONE_OK); /* Evicted handles remain valid. */
  assert(limestone_runtime_invalidate(host.runtime,UINT64_MAX,2,&invalidated,&error)==LIMESTONE_INVALID_ARGUMENT&&invalidated==999);
  assert(limestone_runtime_invalidate(host.runtime,100,0,&invalidated,NULL)==LIMESTONE_OK&&invalidated==0);
  assert(limestone_runtime_invalidate(host.runtime,101,1,&invalidated,NULL)==LIMESTONE_OK&&invalidated==2);
  assert(!limestone_translated_region_is_valid(cold)&&!limestone_translated_region_is_valid(hot)&&limestone_translated_region_is_valid(other));
  assert(limestone_runtime_invoke(host.runtime,hot,&result,NULL)==LIMESTONE_CONFLICT&&host.releases==0);
  limestone_translated_region_destroy(cold);limestone_translated_region_destroy(hot);assert(host.releases==1);
  limestone_runtime_destroy(host.runtime);host.runtime=NULL;
  assert(!limestone_translated_region_is_valid(other)&&limestone_translated_region_get_view(other,&view,NULL)==LIMESTONE_OK&&view.bytes[0]==9);limestone_translated_region_destroy(other);limestone_runtime_destroy(foreign);

  options.hot_threshold=1;
  host.runtime=limestone_runtime_create(source,target,&options,install,&host,&error);assert(host.runtime);
  host.install_mode=1;assert(!limestone_runtime_prepare(host.runtime,bytes,2,100,200,&error)&&error.code==LIMESTONE_UNSUPPORTED&&strcmp(error.message,"host installation unsupported")==0&&host.releases==2);
  host.install_mode=2;assert(!limestone_runtime_prepare(host.runtime,bytes,2,100,200,&error)&&error.code==LIMESTONE_CONFLICT&&host.releases==3);
  host.install_mode=5;assert(!limestone_runtime_prepare(host.runtime,bytes,2,100,200,&error)&&error.code==LIMESTONE_INTERRUPTED&&strlen(error.message)==511&&host.releases==4);
  host.install_mode=3;assert(!limestone_runtime_prepare(host.runtime,bytes,2,100,200,&error)&&error.code==LIMESTONE_INTERRUPTED&&host.releases==5&&limestone_runtime_resident_count(host.runtime)==0);
  host.install_mode=4;host.active=limestone_runtime_prepare(host.runtime,bytes,2,100,200,&error);assert(host.active&&limestone_translated_region_is_compiled(host.active));
  limestone_binary_architecture_destroy(source);limestone_binary_architecture_destroy(target);
  host.execute_mode=1;assert(limestone_runtime_invoke(host.runtime,host.active,&result,&error)==LIMESTONE_OK&&result==42&&!host.active&&host.releases==6&&limestone_runtime_resident_count(host.runtime)==0);
  assert(!limestone_runtime_prepare(host.runtime,NULL,1,0,0,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
  assert(!limestone_runtime_prepare(host.runtime,NULL,0,0,0,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
  assert(limestone_runtime_invoke(host.runtime,NULL,&result,NULL)==LIMESTONE_INVALID_ARGUMENT);
  assert(limestone_translated_region_get_view(NULL,&view,NULL)==LIMESTONE_INVALID_ARGUMENT);
  assert(!limestone_translated_region_is_valid(NULL)&&!limestone_translated_region_is_compiled(NULL));
  limestone_translated_region_destroy(NULL);limestone_runtime_destroy(host.runtime);limestone_runtime_destroy(NULL);
}
