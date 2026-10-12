#include "native_c.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cerrno>
#include <cstring>
#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifndef LIMESTONE_NATIVE_CC
#define LIMESTONE_NATIVE_CC "cc"
#endif

namespace limestone::machineir_native {
struct LibraryStorage {
  void* handle=nullptr;
  std::vector<uint8_t> image;
  std::string target;
  ~LibraryStorage() {
#if defined(__unix__) || defined(__APPLE__)
    if(handle) dlclose(handle);
#endif
  }
};
namespace {
[[noreturn]] void fail(std::string message,Error::Code code=Error::Code::Internal) {throw Error{code,"MachineIR native toolchain: "+message};}
struct Directory {
  std::filesystem::path path;
  Directory() {
#if defined(__unix__) || defined(__APPLE__)
    auto root=std::filesystem::exists("/tmp/opencode")?std::filesystem::path("/tmp/opencode"):std::filesystem::temp_directory_path();
    auto pattern=(root/"machineir-native-XXXXXX").string();
    std::vector<char> name(pattern.begin(),pattern.end()); name.push_back(0);
    if(!mkdtemp(name.data())) fail("cannot create build directory: "+std::string(std::strerror(errno)));
    path=name.data();
#else
    fail("native host toolchain requires POSIX process/loading support",Error::Code::Unsupported);
#endif
  }
  ~Directory() {std::error_code error; if(!path.empty()) std::filesystem::remove_all(path,error);}
};
std::string read(const std::filesystem::path& path,size_t limit) {
  std::error_code error; auto size=std::filesystem::file_size(path,error);
  if(error) fail("cannot read compiler artifact '"+path.filename().string()+"'");
  if(size>limit) fail("compiler artifact exceeds configured size limit",Error::Code::ResourceLimit);
  std::ifstream in(path,std::ios::binary); std::string text(static_cast<size_t>(size),'\0');
  if(!in.read(text.data(),static_cast<std::streamsize>(size))) fail("truncated compiler artifact");
  return text;
}
std::string compiler(const Options& options) {
  auto result=options.compiler.empty()?std::string(LIMESTONE_NATIVE_CC):options.compiler;
  if(result.empty() || result.find('\0')!=std::string::npos) fail("invalid compiler executable",Error::Code::InvalidArgument);
  if(std::filesystem::path(result).has_parent_path()) result=std::filesystem::absolute(result).string();
  return result;
}
void run(const std::vector<std::string>& arguments,const Directory& dir,uint32_t timeout) {
#if defined(__unix__) || defined(__APPLE__)
  if(!timeout) fail("compiler timeout must be positive",Error::Code::InvalidArgument);
  std::vector<char*> argv; argv.reserve(arguments.size()+1);
  for(const auto& a:arguments) {
    if(a.find('\0')!=std::string::npos) fail("NUL in compiler argument",Error::Code::InvalidArgument);
    argv.push_back(const_cast<char*>(a.c_str()));
  }
  argv.push_back(nullptr);
  const auto log=(dir.path/"compiler.log").string();
  int fd=open(log.c_str(),O_CREAT|O_WRONLY|O_TRUNC,0600); if(fd<0) fail("cannot open compiler diagnostics");
  auto cwd=dir.path.string(); auto child=fork();
  if(child<0) {close(fd); fail("cannot start C compiler");}
  if(child==0) {
    setpgid(0,0);
    if(chdir(cwd.c_str()) || dup2(fd,STDOUT_FILENO)<0 || dup2(fd,STDERR_FILENO)<0) _exit(126);
    close(fd); execvp(argv[0],argv.data());
    const char message[]="cannot execute native C compiler\n"; (void)write(STDERR_FILENO,message,sizeof(message)-1); _exit(127);
  }
  close(fd); (void)setpgid(child,child);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(timeout);
  int status=0;
  for(;;) {
    auto result=waitpid(child,&status,WNOHANG);
    if(result==child) break;
    if(result<0 && errno!=EINTR) {kill(-child,SIGKILL); while(waitpid(child,&status,0)<0 && errno==EINTR) {} fail("cannot wait for native compiler");}
    if(std::chrono::steady_clock::now()>=deadline) {
      kill(-child,SIGKILL); while(waitpid(child,&status,0)<0 && errno==EINTR) {}
      fail("C compiler timed out",Error::Code::Timeout);
    }
    usleep(10000);
  }
  if(!WIFEXITED(status) || WEXITSTATUS(status)!=0) {
    std::ifstream in(log); std::string diagnostic(16384,'\0'); in.read(diagnostic.data(),diagnostic.size()); diagnostic.resize(static_cast<size_t>(in.gcount()));
    fail("C compilation failed:\n"+diagnostic,Error::Code::Parse);
  }
#else
  (void)arguments; (void)dir; (void)timeout;
  fail("native compilation is unavailable on this host",Error::Code::Unsupported);
#endif
}
void source(const Unit& unit,const Directory& dir) {
  auto text=emit_c(unit); if(!text) throw text.error();
  std::ofstream out(dir.path/"module.c",std::ios::binary);
  if(!out || !(out<<text.value())) fail("cannot write MachineIR C lowering");
}
template<class T,class F> Result<T> checked(F&& f) {
  try {return Result<T>::ok(f());}
  catch(const Error& e) {return Result<T>::err(e);}
  catch(const std::bad_alloc&) {return Result<T>::err({Error::Code::ResourceLimit,"MachineIR native allocation failure"});}
  catch(const std::exception& e) {return Result<T>::err({Error::Code::Internal,e.what()});}
}
}
Library::Library(std::shared_ptr<LibraryStorage> s):storage_(std::move(s)) {}
Result<void*> Library::symbol(const std::string& name) const {
#if defined(__unix__) || defined(__APPLE__)
  if(!storage_ || name.find('\0')!=std::string::npos) return Result<void*>::err({Error::Code::InvalidArgument,"MachineIR native: invalid library/symbol"});
  dlerror(); auto pointer=dlsym(storage_->handle,name.c_str()); auto error=dlerror();
  if(error || !pointer) return Result<void*>::err({Error::Code::NotFound,"MachineIR native: missing symbol '"+name+"': "+(error?error:"null address")});
  return Result<void*>::ok(pointer);
#else
  (void)name; return Result<void*>::err({Error::Code::Unsupported,"native loading unavailable"});
#endif
}
std::span<const uint8_t> Library::image() const {return storage_?std::span<const uint8_t>(storage_->image):std::span<const uint8_t>{};}
std::string_view Library::target() const {return storage_?std::string_view(storage_->target):std::string_view{};}
Result<std::string> emit_assembly(const Unit& unit,Options options) {
  return checked<std::string>([&] {
    Directory dir; source(unit,dir);
    std::vector<std::string> args={compiler(options),"-std=c99","-O2","-fPIC"};
    args.insert(args.end(),options.compile_arguments.begin(),options.compile_arguments.end());
    args.insert(args.end(),{"-S","module.c","-o","module.s"});
    run(args,dir,options.timeout_seconds); return read(dir.path/"module.s",options.image_limit);
  });
}
Result<Library> compile(const Unit& unit,Options options) {
  return checked<Library>([&] {
#if defined(__unix__) || defined(__APPLE__)
    Directory dir; source(unit,dir);
    std::vector<std::string> args={compiler(options),"-std=c99","-O2","-fPIC"};
    args.insert(args.end(),options.compile_arguments.begin(),options.compile_arguments.end());
    args.insert(args.end(),{"-shared","module.c","-o","module.so"});
#if !defined(__APPLE__)
    args.insert(args.end(),{"-Wl,-z,defs","-Wl,-Bsymbolic"});
#endif
    args.insert(args.end(),options.link_arguments.begin(),options.link_arguments.end());
    run(args,dir,options.timeout_seconds);
    auto image=read(dir.path/"module.so",options.image_limit);
    auto storage=std::make_shared<LibraryStorage>(); storage->image.assign(image.begin(),image.end());
    run({compiler(options),"-dumpmachine"},dir,options.timeout_seconds);
    storage->target=read(dir.path/"compiler.log",4096);
    while(!storage->target.empty() && (storage->target.back()=='\n' || storage->target.back()=='\r')) storage->target.pop_back();
    storage->handle=dlopen((dir.path/"module.so").c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!storage->handle) {auto error=dlerror(); fail("cannot load native image: "+std::string(error?error:"unknown loader error"),Error::Code::Unsupported);}
    return Library(std::move(storage)); // POSIX loader owns mapped code after files are removed.
#else
    (void)unit; (void)options; fail("native loading unavailable",Error::Code::Unsupported);
#endif
  });
}
} // namespace limestone::machineir_native
